/*
 * otd-wintab GTK integration test.
 *
 * A minimal GTK 3 application that runs on a real GTK 3 runtime (GIMP's,
 * found through PATH), loads otd-wintab through the dwmapi proxy exactly
 * like GIMP does, and reproduces GIMP 3's input-device logic
 * (app/widgets/gimpdevices.c, gimpdevicemanager.c and the stroke grab in
 * gimpdisplayshell-tool-events.c). It then plays scripted styli through
 * the ring, moves the real mouse with SendInput, and checks after each
 * step which device GIMP would consider current.
 *
 * It takes over the mouse pointer for a few seconds; see
 * scripts/test-gtk.ps1.
 *
 * Copyright 2026 otd-wintab contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdio.h>
#include <string.h>

#include "wine_compat.h"
#include "ring.h"

/* ---- the slice of the GTK 3 API we use (linked via import libraries) -- */

typedef int gboolean;
typedef struct _GList { void *data; struct _GList *next, *prev; } GList;

enum { GDK_MOTION_NOTIFY = 3, GDK_BUTTON_PRESS = 4, GDK_BUTTON_RELEASE = 7 };
enum { GDK_MODE_DISABLED = 0, GDK_MODE_SCREEN = 1 };
enum { GDK_DEVICE_TYPE_MASTER = 0, GDK_DEVICE_TYPE_SLAVE = 1, GDK_DEVICE_TYPE_FLOATING = 2 };
enum { GDK_SOURCE_MOUSE = 0, GDK_SOURCE_PEN = 1, GDK_SOURCE_ERASER = 2, GDK_SOURCE_CURSOR = 3,
       GDK_SOURCE_KEYBOARD = 4 };
enum { GDK_AXIS_PRESSURE = 3, GDK_AXIS_WHEEL = 6 };
#define GDK_ALL_EVENTS_MASK   0x3FFFFFE
#define GDK_SEAT_CAPABILITY_ALL 15

void      gtk_init(int *argc, char ***argv);
void     *gtk_window_new(int type);
void      gtk_window_set_title(void *window, const char *title);
void      gtk_window_set_default_size(void *window, int w, int h);
void      gtk_window_move(void *window, int x, int y);
void      gtk_window_set_keep_above(void *window, gboolean setting);
void      gtk_window_present(void *window);
void      gtk_widget_add_events(void *widget, int events);
void      gtk_widget_show_all(void *widget);
void     *gtk_widget_get_window(void *widget);
gboolean  gtk_events_pending(void);
gboolean  gtk_main_iteration_do(gboolean blocking);

void     *gdk_display_get_default(void);
void     *gdk_display_get_default_seat(void *display);
void     *gdk_seat_get_pointer(void *seat);
GList    *gdk_seat_get_slaves(void *seat, int capabilities);
void     *gdk_event_get_source_device(const void *event);
int       gdk_event_get_event_type(const void *event);
gboolean  gdk_event_get_axis(const void *event, int axis, double *value);
const char *gdk_device_get_name(void *device);
int       gdk_device_get_mode(void *device);
gboolean  gdk_device_set_mode(void *device, int mode);
int       gdk_device_get_device_type(void *device);
int       gdk_device_get_source(void *device);
void     *gdk_device_get_associated_device(void *device);
int       gdk_window_get_origin(void *window, int *x, int *y);
int       gdk_window_get_scale_factor(void *window);

unsigned long g_signal_connect_data(void *instance, const char *signal, void *handler,
                                    void *data, void *destroy, int flags);
void      g_list_free(GList *list);

/* ---- GIMP's device bookkeeping ------------------------------------------ */

#define MAX_DEVICES 64
static void *known[MAX_DEVICES];
static int nknown;
static void *seat, *core_pointer;
static void *current;               /* gimp_device_manager_get_current_device */
static void *grab_source;           /* shell->grab_pointer_source */
static int ignored_while_grabbed;
static BOOL verbose;                /* OTD_GTKPROBE_VERBOSE=1: print every event */
static BOOL os_button_down;         /* the real left mouse button, see os_button() */

struct step_stats
{
    int presses, releases, motions;
    double max_pressure, max_wheel;
    void *pressed_on;
    /* Wheel axis range seen while hovering and while pressed */
    double hover_wheel_min, hover_wheel_max, down_wheel_min, down_wheel_max;
    int hover_wheel_n, down_wheel_n;
    int pen_events_down, core_events_down;  /* while the OS button is held */
};
static struct step_stats st;

static BOOL is_known(void *d)
{
    int i;
    for (i = 0; i < nknown; i++)
        if (known[i] == d) return TRUE;
    return FALSE;
}

/* gimp_device_manager_device_defaults() + device_added() */
static void device_added(void *s, void *device, void *data)
{
    if (gdk_device_get_source(device) == GDK_SOURCE_KEYBOARD) return;
    if (device == gdk_seat_get_pointer(seat))
        gdk_device_set_mode(device, GDK_MODE_SCREEN);
    else if (gdk_device_get_device_type(device) != GDK_DEVICE_TYPE_MASTER)
    {
        int src = gdk_device_get_source(device);
        gdk_device_set_mode(device, (src == GDK_SOURCE_PEN || src == GDK_SOURCE_ERASER ||
                                     src == GDK_SOURCE_CURSOR) ? GDK_MODE_SCREEN : GDK_MODE_DISABLED);
    }
    if (!is_known(device) && nknown < MAX_DEVICES)
    {
        known[nknown++] = device;
        printf("  device: %-48s %s\n", gdk_device_get_name(device),
               gdk_device_get_mode(device) == GDK_MODE_DISABLED ? "(disabled)" : "");
    }
}

/* gimp_devices_get_from_event() */
static void *device_from_event(const void *event)
{
    void *d = gdk_event_get_source_device(event);
    if (!d || gdk_device_get_source(d) == GDK_SOURCE_KEYBOARD) return NULL;
    switch (gdk_device_get_device_type(d))
    {
    case GDK_DEVICE_TYPE_MASTER:
        return NULL;
    case GDK_DEVICE_TYPE_SLAVE:
        return gdk_device_get_mode(d) == GDK_MODE_DISABLED ? gdk_device_get_associated_device(d) : d;
    default:
        return d;
    }
}

static gboolean on_event(void *widget, void *event, void *data)
{
    int type = gdk_event_get_event_type(event);
    void *d;
    double v;

    if (type != GDK_MOTION_NOTIFY && type != GDK_BUTTON_PRESS && type != GDK_BUTTON_RELEASE)
        return FALSE;
    if (verbose)
        printf("      %-8s source=%s\n", type == GDK_MOTION_NOTIFY ? "motion" :
               type == GDK_BUTTON_PRESS ? "press" : "release",
               gdk_event_get_source_device(event) ?
               gdk_device_get_name(gdk_event_get_source_device(event)) : "(none)");
    if (!(d = device_from_event(event))) return FALSE;

    if (os_button_down)
    {
        if (d == core_pointer) st.core_events_down++;
        else st.pen_events_down++;
    }

    /* while a stroke holds the grab, other devices are ignored */
    if (grab_source && d != grab_source)
    {
        ignored_while_grabbed++;
        return FALSE;
    }

    /* gimp_devices_check_change(): unknown devices keep the current one */
    if (is_known(d) && d != current)
        current = d;

    if (type == GDK_BUTTON_PRESS)
    {
        grab_source = d;
        st.presses++;
        st.pressed_on = d;
    }
    else if (type == GDK_BUTTON_RELEASE)
    {
        if (d == grab_source) grab_source = NULL;
        st.releases++;
    }
    else
        st.motions++;

    if (gdk_event_get_axis(event, GDK_AXIS_PRESSURE, &v) && v > st.max_pressure) st.max_pressure = v;
    if (gdk_event_get_axis(event, GDK_AXIS_WHEEL, &v))
    {
        BOOL down = grab_source != NULL;
        double *mn = down ? &st.down_wheel_min : &st.hover_wheel_min;
        double *mx = down ? &st.down_wheel_max : &st.hover_wheel_max;
        int *n = down ? &st.down_wheel_n : &st.hover_wheel_n;
        if (!*n || v < *mn) *mn = v;
        if (!*n || v > *mx) *mx = v;
        (*n)++;
        if (v > st.max_wheel) st.max_wheel = v;
    }
    return FALSE;
}

/* ---- drivers ------------------------------------------------------------- */

static struct ring_handle ring;
static int win_x, win_y;            /* screen position of the test area */
static int failures;

static void pump(int ms)
{
    DWORD start = GetTickCount();
    while (GetTickCount() - start < (DWORD)ms)
    {
        while (gtk_events_pending()) gtk_main_iteration_do(FALSE);
        Sleep(2);
    }
}

static void mouse_to(int x, int y)
{
    INPUT in = {0};
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    in.type = INPUT_MOUSE;
    in.mi.dx = (LONG)(((double)(x - vx) + 0.5) * 65536.0 / vw);
    in.mi.dy = (LONG)(((double)(y - vy) + 0.5) * 65536.0 / vh);
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &in, sizeof(in));
}

/* OpenTabletDriver's tip binding: the real left mouse button follows the tip */
static void os_button(BOOL down)
{
    INPUT in = {0};
    if (down == os_button_down) return;
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    SendInput(1, &in, sizeof(in));
    os_button_down = down;
}

static void mouse_wiggle(void)
{
    int i;
    for (i = 0; i < 10; i++)
    {
        mouse_to(win_x + 200 + i * 7, win_y + 150 + (i & 1) * 9);
        pump(15);
    }
    pump(100);
}

struct tool { const char *name; uint32_t tool_id, serial, flags, axes; };
static const struct tool GRIP   = { "Grip Pen", 0x823, 0xA001, 0, OTDWT_AX_PRESSURE | OTDWT_AX_TILT };
static const struct tool ERASER = { "Grip Pen Eraser", 0x82b, 0xA001, OTDWT_F_ERASER, OTDWT_AX_PRESSURE | OTDWT_AX_TILT };
static const struct tool AIR    = { "Airbrush", 0x913, 0xB002, 0, OTDWT_AX_PRESSURE | OTDWT_AX_TILT | OTDWT_AX_WHEEL };
static const struct tool ART    = { "6D Art Pen", 0x885, 0xC003, 0, OTDWT_AX_PRESSURE | OTDWT_AX_TILT | OTDWT_AX_ROTATION };

static int32_t pen_rotation;        /* tenths of a degree, for the next events */

static void pen(uint32_t type, const struct tool *t, int x, int y, uint32_t pressure)
{
    otdwt_event ev = {0};
    ev.type = type;
    ev.flags = t->flags;
    ev.axes = t->axes;
    ev.tool_id = t->tool_id;
    ev.serial = t->serial;
    ev.buttons = pressure ? 1 : 0;
    ev.x = (x - GetSystemMetrics(SM_XVIRTUALSCREEN)) * OTDWT_POS_SCALE;
    ev.y = (y - GetSystemMetrics(SM_YVIRTUALSCREEN)) * OTDWT_POS_SCALE;
    ev.pressure = pressure;
    ev.tilt_x = 150;
    ev.wheel = 600;
    ev.rotation = pen_rotation;
    ev.time_ms = GetTickCount();
    strncpy(ev.name, t->name, OTDWT_NAME_LEN - 1);
    ring_publish(&ring, &ev);
}

/* a real pen: the system cursor follows it, as OpenTabletDriver does */
static void pen_stroke(const struct tool *t, BOOL lift_tip)
{
    int i, x = win_x + 150, y = win_y + 120;

    mouse_to(x, y);
    pen(OTDWT_EV_PROXIMITY_IN, t, x, y, 0);
    pump(60);
    for (i = 0; i < 20; i++)
    {
        BOOL down = i > 2 && i < 17;
        x += 8; y += 3;
        mouse_to(x, y);
        pen(OTDWT_EV_PACKET, t, x, y, down ? 20000 : 0);
        os_button(down);
        pump(15);
    }
    if (!lift_tip)
    {
        pen(OTDWT_EV_PACKET, t, x, y, 20000);    /* tip goes down again ... */
        os_button(TRUE);
        pump(30);
    }
    pen(OTDWT_EV_PROXIMITY_OUT, t, x, y, 0);     /* ... and the pen leaves */
    os_button(FALSE);                            /* the fixed parser releases it */
    pump(150);
}

/* the Art Pen turning its barrel: hovering, then with the tip down */
static void art_pen_twisting(void)
{
    int i, x = win_x + 150, y = win_y + 120;

    pen_rotation = 0;
    mouse_to(x, y);
    pen(OTDWT_EV_PROXIMITY_IN, &ART, x, y, 0);
    pump(60);
    for (i = 0; i < 40; i++)
    {
        x += 4; y += 2;
        pen_rotation = (i * 85) % 3600;     /* keeps turning throughout */
        mouse_to(x, y);
        pen(OTDWT_EV_PACKET, &ART, x, y, i >= 20 ? 20000 : 0);
        os_button(i >= 20);
        pump(15);
    }
    pen(OTDWT_EV_PACKET, &ART, x, y, 0);
    os_button(FALSE);
    pump(60);                               /* hover a moment before leaving */
    pen(OTDWT_EV_PROXIMITY_OUT, &ART, x, y, 0);
    pump(150);
    pen_rotation = 0;
}

static const char *name_of(void *d) { return d ? gdk_device_get_name(d) : "(none)"; }

static void expect_current(const char *step, const char *substr, void *exact)
{
    BOOL ok = exact ? current == exact : (current && strstr(name_of(current), substr) != NULL);
    printf("%-44s current: %-40s %s\n", step, name_of(current), ok ? "ok" : "FAILED");
    if (!ok) failures++;
}

static void expect(BOOL cond, const char *what)
{
    if (!cond)
    {
        printf("    FAILED: %s\n", what);
        failures++;
    }
}

static void begin(void) { memset(&st, 0, sizeof(st)); }

int main(void)
{
    void *window, *display;
    GList *slaves, *l;
    POINT saved;

    setvbuf(stdout, NULL, _IONBF, 0);
    GetCursorPos(&saved);
    verbose = GetEnvironmentVariableA("OTD_GTKPROBE_VERBOSE", NULL, 0) > 0;

    /* the producer side first, so the provider attaches immediately */
    if (!ring_create(&ring))
    {
        printf("cannot create ring\n");
        return 2;
    }

    SetEnvironmentVariableA("GDK_WIN32_TABLET_INPUT_API", "wintab");
    gtk_init(NULL, NULL);

    display = gdk_display_get_default();
    seat = gdk_display_get_default_seat(display);
    core_pointer = gdk_seat_get_pointer(seat);
    current = core_pointer;

    /* gimp_device_manager_constructed(): startup enumeration + signals */
    printf("devices at startup:\n");
    device_added(seat, core_pointer, NULL);
    slaves = gdk_seat_get_slaves(seat, GDK_SEAT_CAPABILITY_ALL);
    for (l = slaves; l; l = l->next) device_added(seat, l->data, NULL);
    g_list_free(slaves);
    g_signal_connect_data(seat, "device-added", (void *)device_added, NULL, NULL, 0);

    window = gtk_window_new(0);
    gtk_window_set_title(window, "otd-wintab GTK test");
    gtk_window_set_default_size(window, 640, 420);
    gtk_window_move(window, 80, 80);
    gtk_window_set_keep_above(window, TRUE);
    gtk_widget_add_events(window, GDK_ALL_EVENTS_MASK);
    g_signal_connect_data(window, "event", (void *)on_event, NULL, NULL, 0);
    gtk_widget_show_all(window);
    gtk_window_present(window);
    pump(500);
    gdk_window_get_origin(gtk_widget_get_window(window), &win_x, &win_y);
    if (gdk_window_get_scale_factor(gtk_widget_get_window(window)) != 1)
        printf("note: GDK window scale is not 1; coordinates may be off\n");

    printf("\nscenario:\n");
    begin(); mouse_wiggle();
    expect_current("mouse", NULL, core_pointer);

    begin(); pen_stroke(&GRIP, TRUE);
    expect_current("grip pen stroke", "Grip Pen #0000A001", NULL);
    expect(st.presses >= 1 && st.releases >= 1, "pen press and release delivered");
    expect(st.max_pressure > 0.1, "pressure reaches the application");
    expect(st.pen_events_down > 0 && st.core_events_down == 0,
           "while the tip is down, events come from the pen, not the core pointer");
    printf("    tip down: %d pen events, %d core pointer events\n", st.pen_events_down, st.core_events_down);

    begin(); mouse_wiggle();
    expect_current("pen lifted, mouse moves", NULL, core_pointer);

    begin(); pen_stroke(&AIR, TRUE);
    expect_current("airbrush stroke", "Airbrush #0000B002", NULL);
    expect(st.max_wheel > 0.1, "airbrush wheel reaches the application");

    begin(); pen_stroke(&GRIP, TRUE);
    expect_current("back to the grip pen", "Grip Pen #0000A001", NULL);

    begin(); mouse_wiggle();
    expect_current("mouse again", NULL, core_pointer);

    begin(); pen_stroke(&GRIP, FALSE);
    begin(); mouse_wiggle();
    expect_current("pen left with tip down, then mouse", NULL, core_pointer);
    expect(grab_source == NULL, "no stroke grab left behind");

    begin(); pen_stroke(&ERASER, TRUE);
    expect_current("eraser end", "Grip Pen Eraser #0000A001", NULL);

    begin(); art_pen_twisting();
    expect_current("art pen twisting", "6D Art Pen #0000C003", NULL);
    printf("    rotation (Wheel axis) hovering: %d events, %.3f..%.3f; tip down: %d events, %.3f..%.3f\n",
           st.hover_wheel_n, st.hover_wheel_min, st.hover_wheel_max,
           st.down_wheel_n, st.down_wheel_min, st.down_wheel_max);
    expect(st.hover_wheel_n > 0 && st.hover_wheel_max - st.hover_wheel_min > 0.1,
           "rotation changes while hovering");
    expect(st.down_wheel_n > 0 && st.down_wheel_max - st.down_wheel_min > 0.1,
           "rotation keeps changing with the tip down");

    begin(); mouse_wiggle();
    expect_current("mouse at the end", NULL, core_pointer);

    os_button(FALSE);
    SetCursorPos(saved.x, saved.y);
    ring_close(&ring);
    printf("\n%s (%d failure(s), %d events ignored during grabs)\n",
           failures ? "GTK integration test FAILED" : "GTK integration test passed",
           failures, ignored_while_grabbed);
    return failures ? 1 : 0;
}
