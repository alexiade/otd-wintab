/*
 * otd-wintab GTK 4 integration test.
 *
 * A minimal GTK 4 application that runs on a real GTK 4 runtime (found
 * through PATH), loads otd-wintab through the dwmapi proxy like the GTK 3
 * test does, and uses tablet input the way GTK 4 drawing applications do:
 * a GtkGestureStylus on the canvas, with the current input tool taken from
 * gdk_event_get_device_tool(). It plays scripted styli through the ring,
 * moves the real mouse with SendInput, and checks after each step what
 * such an application sees.
 *
 * GTK 4 uses Windows Ink (WinPointer) by default; the test selects its
 * Wintab backend with GDK_WIN32_TABLET_INPUT_API=wintab.
 *
 * It takes over the mouse pointer for a few seconds; see
 * scripts/test-gtk4.ps1.
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

/* ---- the slice of the GTK 4 API we use (linked via import libraries) -- */

typedef int gboolean;
typedef unsigned long long guint64;
typedef struct _GList { void *data; struct _GList *next, *prev; } GList;

enum { GDK_MOTION_NOTIFY = 1, GDK_BUTTON_PRESS = 2, GDK_BUTTON_RELEASE = 3,
       GDK_ENTER_NOTIFY = 6, GDK_LEAVE_NOTIFY = 7,
       GDK_PROXIMITY_IN = 9, GDK_PROXIMITY_OUT = 10 };
enum { GDK_AXIS_PRESSURE = 5, GDK_AXIS_XTILT = 6, GDK_AXIS_YTILT = 7, GDK_AXIS_WHEEL = 8,
       GDK_AXIS_ROTATION = 10 };
enum { GDK_SEAT_CAPABILITY_TABLET_STYLUS = 4 };
enum { GDK_DEVICE_TOOL_TYPE_UNKNOWN, GDK_DEVICE_TOOL_TYPE_PEN, GDK_DEVICE_TOOL_TYPE_ERASER,
       GDK_DEVICE_TOOL_TYPE_BRUSH, GDK_DEVICE_TOOL_TYPE_PENCIL, GDK_DEVICE_TOOL_TYPE_AIRBRUSH,
       GDK_DEVICE_TOOL_TYPE_MOUSE, GDK_DEVICE_TOOL_TYPE_LENS };

void      gtk_init(void);
void     *gtk_window_new(void);
void      gtk_window_set_title(void *window, const char *title);
void      gtk_window_set_default_size(void *window, int w, int h);
void      gtk_window_set_child(void *window, void *child);
void      gtk_window_present(void *window);
void     *gtk_drawing_area_new(void);
void      gtk_widget_add_controller(void *widget, void *controller);
void     *gtk_widget_get_native(void *widget);
void     *gtk_native_get_surface(void *native);
void     *gtk_event_controller_legacy_new(void);
void     *gtk_gesture_stylus_new(void);
gboolean  gtk_gesture_stylus_get_axis(void *gesture, int axis, double *value);
void     *gtk_gesture_stylus_get_device_tool(void *gesture);

void     *gdk_win32_surface_get_handle(void *surface);
void     *gdk_display_get_default(void);
void     *gdk_display_get_default_seat(void *display);
GList    *gdk_seat_get_devices(void *seat, int capabilities);
GList    *gdk_seat_get_tools(void *seat);
const char *gdk_device_get_name(void *device);
int       gdk_event_get_event_type(void *event);
void     *gdk_event_get_device_tool(void *event);
gboolean  gdk_event_get_axis(void *event, int axis, double *value);
guint64   gdk_device_tool_get_serial(void *tool);
guint64   gdk_device_tool_get_hardware_id(void *tool);
int       gdk_device_tool_get_tool_type(void *tool);

gboolean  g_main_context_iteration(void *context, gboolean may_block);
unsigned long g_signal_connect_data(void *instance, const char *signal, void *handler,
                                    void *data, void *destroy, int flags);
void      g_list_free(GList *list);

/* ---- what a drawing application tracks -------------------------------- */

static void *seat;
static void *current;               /* the event's tool; NULL: the mouse */
static BOOL verbose;                /* OTD_GTKPROBE_VERBOSE=1: print every event */
static BOOL os_button_down;         /* the real left mouse button, see os_button() */
static int ndevices_added, ntools_added;

struct step_stats
{
    int downs, ups, stylus_motions;  /* GtkGestureStylus */
    BOOL stylus_is_down;
    double max_pressure, max_wheel, max_tilt;
    void *down_tool;                 /* gtk_gesture_stylus_get_device_tool() on "down" */
    double hover_rot_min, hover_rot_max, down_rot_min, down_rot_max;
    int hover_rot_n, down_rot_n;
    int tool_events_down, mouse_events_down;  /* while the OS button is held */
    int switches_during_stroke;               /* current tool changed while down */
    BOOL second_stroke;                       /* pen_two_strokes(): in the second contact */
    int second_with_pressure, second_without_pressure;
};
static struct step_stats st;
static BOOL stylus_down;            /* across steps: a "down" without an "up" */

#define EVLOG 600
static char evlog[EVLOG][160];
static int nevlog;
static void record_event(const char *line)
{
    if (nevlog < EVLOG) strcpy(evlog[nevlog++], line);
    if (verbose) printf("      %s\n", line);
}

static void dump_events(void)
{
    int i, run = 1;
    for (i = 0; i < nevlog; i++)
    {
        if (i + 1 < nevlog && !strcmp(evlog[i], evlog[i + 1])) { run++; continue; }
        printf("      %3dx %s\n", run, evlog[i]);
        run = 1;
    }
}

static const char *tool_type_name(int type)
{
    static const char *const names[] = { "unknown", "pen", "eraser", "brush", "pencil",
                                         "airbrush", "mouse", "lens" };
    return type >= 0 && type < 8 ? names[type] : "?";
}

static const char *describe(void *tool)
{
    static char buf[4][64];
    static int n;
    char *b = buf[n++ & 3];
    if (!tool) return "(mouse)";
    snprintf(b, 64, "%s #%08llX type %03llX", tool_type_name(gdk_device_tool_get_tool_type(tool)),
             gdk_device_tool_get_serial(tool), gdk_device_tool_get_hardware_id(tool));
    return b;
}

/* GtkEventControllerLegacy: every event the canvas gets */
static gboolean on_event(void *controller, void *event, void *data)
{
    int type = gdk_event_get_event_type(event);
    void *tool;
    char line[160];
    double p = -1, r = -1;

    if (type != GDK_MOTION_NOTIFY && type != GDK_BUTTON_PRESS && type != GDK_BUTTON_RELEASE &&
        type != GDK_ENTER_NOTIFY && type != GDK_LEAVE_NOTIFY && type != GDK_PROXIMITY_IN &&
        type != GDK_PROXIMITY_OUT)
        return FALSE;

    tool = gdk_event_get_device_tool(event);
    gdk_event_get_axis(event, GDK_AXIS_PRESSURE, &p);
    gdk_event_get_axis(event, GDK_AXIS_ROTATION, &r);
    snprintf(line, sizeof(line), "%-8s %-36s p=%5.2f rot=%5.2f%s%s",
             type == GDK_MOTION_NOTIFY ? "motion" : type == GDK_BUTTON_PRESS ? "press" :
             type == GDK_BUTTON_RELEASE ? "release" : type == GDK_ENTER_NOTIFY ? "enter" :
             type == GDK_LEAVE_NOTIFY ? "leave" : type == GDK_PROXIMITY_IN ? "prox-in" : "prox-out",
             describe(tool), p, r, stylus_down ? "  [down]" : "", os_button_down ? "  [os button]" : "");
    record_event(line);

    if (type != GDK_MOTION_NOTIFY && type != GDK_BUTTON_PRESS && type != GDK_BUTTON_RELEASE)
        return FALSE;

    if (os_button_down)
    {
        if (tool) st.tool_events_down++;
        else st.mouse_events_down++;
    }
    if (tool != current)
    {
        if (stylus_down) st.switches_during_stroke++;
        current = tool;
    }
    return FALSE;
}

static void stylus_axes(void *gesture)
{
    double v;
    if (gtk_gesture_stylus_get_axis(gesture, GDK_AXIS_PRESSURE, &v))
    {
        if (v > st.max_pressure) st.max_pressure = v;
        if (st.second_stroke)
        {
            if (v > 0.1) st.second_with_pressure++;
            else st.second_without_pressure++;
        }
    }
    if (gtk_gesture_stylus_get_axis(gesture, GDK_AXIS_XTILT, &v) && v > st.max_tilt) st.max_tilt = v;
    if (gtk_gesture_stylus_get_axis(gesture, GDK_AXIS_WHEEL, &v) && v > st.max_wheel) st.max_wheel = v;
    if (gtk_gesture_stylus_get_axis(gesture, GDK_AXIS_ROTATION, &v))
    {
        double *mn = stylus_down ? &st.down_rot_min : &st.hover_rot_min;
        double *mx = stylus_down ? &st.down_rot_max : &st.hover_rot_max;
        int *n = stylus_down ? &st.down_rot_n : &st.hover_rot_n;
        if (!*n || v < *mn) *mn = v;
        if (!*n || v > *mx) *mx = v;
        (*n)++;
    }
}

static void on_stylus_down(void *gesture, double x, double y, void *data)
{
    st.downs++;
    stylus_down = st.stylus_is_down = TRUE;
    st.down_tool = gtk_gesture_stylus_get_device_tool(gesture);
    stylus_axes(gesture);
}

static void on_stylus_motion(void *gesture, double x, double y, void *data)
{
    st.stylus_motions++;
    stylus_axes(gesture);
}

static void on_stylus_up(void *gesture, double x, double y, void *data)
{
    st.ups++;
    stylus_down = st.stylus_is_down = FALSE;
}

static void on_device_added(void *s, void *device, void *data) { ndevices_added++; }
static void on_tool_added(void *s, void *tool, void *data) { ntools_added++; }

/* ---- drivers ------------------------------------------------------------- */

static struct ring_handle ring;
static int win_x, win_y;            /* screen position of the window's client area */
static int failures;

static void pump(int ms)
{
    DWORD start = GetTickCount();
    while (GetTickCount() - start < (DWORD)ms)
    {
        while (g_main_context_iteration(NULL, FALSE));
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
static BOOL no_os_button;           /* OTD_GTKPROBE_NO_OSBUTTON=1: diagnose without it */
static void os_button(BOOL down)
{
    INPUT in = {0};
    if (no_os_button || down == os_button_down) return;
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
        mouse_to(win_x + 260 + i * 7, win_y + 200 + (i & 1) * 9);
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
    int i, x = win_x + 150, y = win_y + 140;

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
    os_button(FALSE);
    pump(150);
}

/* two strokes without leaving proximity: lift the tip, hover, touch again */
static void pen_two_strokes(const struct tool *t)
{
    int s, i, x = win_x + 150, y = win_y + 140;

    mouse_to(x, y);
    pen(OTDWT_EV_PROXIMITY_IN, t, x, y, 0);
    pump(60);
    for (s = 0; s < 2; s++)
    {
        for (i = 0; i < 16; i++)
        {
            BOOL down = i >= 3 && i < 13;
            x += 6; y += 2;
            mouse_to(x, y);
            pen(OTDWT_EV_PACKET, t, x, y, down ? 20000 : 0);
            os_button(down);
            st.second_stroke = s == 1 && down;
            pump(15);
        }
        st.second_stroke = FALSE;
        pump(100);              /* hovering between the strokes */
    }
    pen(OTDWT_EV_PROXIMITY_OUT, t, x, y, 0);
    pump(150);
}

/* the Art Pen turning its barrel: hovering, then with the tip down */
static void art_pen_twisting(void)
{
    int i, x = win_x + 150, y = win_y + 140;

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

static BOOL is_tool(void *tool, int type, uint32_t serial)
{
    return tool && gdk_device_tool_get_tool_type(tool) == type &&
           gdk_device_tool_get_serial(tool) == serial;
}

static void check(const char *step, BOOL ok)
{
    printf("%-44s current: %-34s %s\n", step, describe(current), ok ? "ok" : "FAILED");
    if (!ok)
    {
        failures++;
        dump_events();
    }
}

static void expect(BOOL cond, const char *what)
{
    if (!cond)
    {
        printf("    FAILED: %s\n", what);
        failures++;
        dump_events();
    }
}

/* a stroke as an application sees it */
static void expect_stroke(void)
{
    expect(st.downs >= 1 && st.ups >= 1, "GtkGestureStylus sees the tip go down and up");
    expect(st.max_pressure > 0.1, "pressure reaches the application");
    expect(st.mouse_events_down == 0, "while the tip is down, no events without the tool");
    expect(st.switches_during_stroke == 0, "the stroke stays on one tool");
}

static void begin(void) { memset(&st, 0, sizeof(st)); nevlog = 0; }

int main(void)
{
    void *window, *canvas, *legacy, *stylus, *grip_tool;
    GList *list, *l;
    POINT saved, origin = {0, 0};
    int n;

    setvbuf(stdout, NULL, _IONBF, 0);
    GetCursorPos(&saved);
    verbose = GetEnvironmentVariableA("OTD_GTKPROBE_VERBOSE", NULL, 0) > 0;
    no_os_button = GetEnvironmentVariableA("OTD_GTKPROBE_NO_OSBUTTON", NULL, 0) > 0;

    /* the producer side first, so the provider attaches immediately */
    if (!ring_create(&ring))
    {
        printf("cannot create ring\n");
        return 2;
    }

    SetEnvironmentVariableA("GDK_WIN32_TABLET_INPUT_API", "wintab");
    gtk_init();

    seat = gdk_display_get_default_seat(gdk_display_get_default());
    g_signal_connect_data(seat, "device-added", (void *)on_device_added, NULL, NULL, 0);
    g_signal_connect_data(seat, "tool-added", (void *)on_tool_added, NULL, NULL, 0);

    window = gtk_window_new();
    gtk_window_set_title(window, "otd-wintab GTK 4 test");
    gtk_window_set_default_size(window, 640, 420);
    canvas = gtk_drawing_area_new();
    gtk_window_set_child(window, canvas);
    legacy = gtk_event_controller_legacy_new();
    g_signal_connect_data(legacy, "event", (void *)on_event, NULL, NULL, 0);
    gtk_widget_add_controller(canvas, legacy);
    stylus = gtk_gesture_stylus_new();
    g_signal_connect_data(stylus, "down", (void *)on_stylus_down, NULL, NULL, 0);
    g_signal_connect_data(stylus, "motion", (void *)on_stylus_motion, NULL, NULL, 0);
    g_signal_connect_data(stylus, "up", (void *)on_stylus_up, NULL, NULL, 0);
    gtk_widget_add_controller(canvas, stylus);
    gtk_window_present(window);
    pump(500);
    {
        HWND hwnd = gdk_win32_surface_get_handle(gtk_native_get_surface(gtk_widget_get_native(window)));
        SetWindowPos(hwnd, HWND_TOPMOST, 80, 80, 0, 0, SWP_NOSIZE);
        SetForegroundWindow(hwnd);
        pump(300);
        ClientToScreen(hwnd, &origin);
        win_x = origin.x;
        win_y = origin.y;
    }

    printf("scenario:\n");
    begin(); mouse_wiggle();
    check("mouse", current == NULL);

    begin(); pen_stroke(&GRIP, TRUE);
    check("grip pen stroke", is_tool(current, GDK_DEVICE_TOOL_TYPE_PEN, 0xA001));
    expect_stroke();
    expect(st.max_tilt > 0.05, "tilt reaches the application");
    printf("    tip down: %d events with the tool, %d without\n",
           st.tool_events_down, st.mouse_events_down);
    grip_tool = st.down_tool;

    begin(); mouse_wiggle();
    check("pen lifted, mouse moves", current == NULL);

    begin(); pen_stroke(&AIR, TRUE);
    check("airbrush stroke", is_tool(current, GDK_DEVICE_TOOL_TYPE_AIRBRUSH, 0xB002));
    expect_stroke();
    expect(st.max_wheel > 0.1, "airbrush wheel reaches the application");

    begin(); pen_stroke(&GRIP, TRUE);
    check("back to the grip pen", is_tool(current, GDK_DEVICE_TOOL_TYPE_PEN, 0xA001));
    expect(grip_tool && st.down_tool == grip_tool, "the grip pen is the same GdkDeviceTool as before");

    begin(); mouse_wiggle();
    check("mouse again", current == NULL);

    begin(); pen_stroke(&GRIP, FALSE);
    begin(); mouse_wiggle();
    check("pen left with tip down, then mouse", current == NULL);
    expect(!stylus_down, "no stylus stroke left behind");

    begin(); pen_stroke(&ERASER, TRUE);
    check("eraser end", is_tool(current, GDK_DEVICE_TOOL_TYPE_ERASER, 0xA001));
    expect_stroke();

    begin(); pen_two_strokes(&GRIP);
    check("two strokes in one proximity", is_tool(current, GDK_DEVICE_TOOL_TYPE_PEN, 0xA001));
    printf("    second stroke: %d stylus events with pressure, %d without\n",
           st.second_with_pressure, st.second_without_pressure);
    expect(st.downs == 2 && st.ups == 2, "both strokes go down and up");
    expect(st.second_with_pressure > 0 && st.second_without_pressure == 0,
           "the second stroke keeps its pressure");

    begin(); art_pen_twisting();
    check("art pen twisting", is_tool(current, GDK_DEVICE_TOOL_TYPE_PEN, 0xC003));
    printf("    rotation hovering: %d events, %.3f..%.3f; tip down: %d events, %.3f..%.3f\n",
           st.hover_rot_n, st.hover_rot_min, st.hover_rot_max,
           st.down_rot_n, st.down_rot_min, st.down_rot_max);
    expect(st.hover_rot_n > 0 && st.hover_rot_max - st.hover_rot_min > 0.1,
           "rotation changes while hovering");
    expect(st.down_rot_n > 0 && st.down_rot_max - st.down_rot_min > 0.1,
           "rotation keeps changing with the tip down");
    expect(st.switches_during_stroke == 0, "the art pen stroke stays on one tool");

    begin(); mouse_wiggle();
    check("mouse at the end", current == NULL);

    printf("\nseat:\n");
    list = gdk_seat_get_devices(seat, GDK_SEAT_CAPABILITY_TABLET_STYLUS);
    for (n = 0, l = list; l; l = l->next, n++) printf("  stylus device: %s\n", gdk_device_get_name(l->data));
    g_list_free(list);
    expect(n >= 1, "the seat lists the tablet");
    printf("  device-added: %d, tool-added: %d\n", ndevices_added, ntools_added);
    expect(ndevices_added >= 1, "the seat announces the tablet");
    list = gdk_seat_get_tools(seat);
    for (n = 0, l = list; l; l = l->next, n++) printf("  tool: %s\n", describe(l->data));
    g_list_free(list);
    expect(n == 4, "the seat knows the four tools used, each once");

    os_button(FALSE);
    SetCursorPos(saved.x, saved.y);
    ring_close(&ring);
    printf("\n%s (%d failure(s))\n",
           failures ? "GTK 4 integration test FAILED" : "GTK 4 integration test passed", failures);
    return failures ? 1 : 0;
}
