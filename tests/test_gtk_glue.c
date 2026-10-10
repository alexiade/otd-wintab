/*
 * otd-wintab tests for the GTK 3 seat glue, against a fake GDK.
 *
 * Copyright 2026 otd-wintab contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>

#include "harness.h"
#include "gtk_glue.h"
#include "iat.h"

/* ---- fake GDK --------------------------------------------------------- */

static int display_obj, seat_obj, manager_obj;
static int devices[4];              /* stand-ins for GdkDevice objects */
static void *floating[8], *slaves[8];
static int nfloating, nslaves;
static void *emitted[16];
static const char *emitted_signal;
static int nemitted, lists_alive;
static BOOL no_display;

static glue_list *make_list(void **items, int n)
{
    glue_list *head = NULL;
    while (n-- > 0)
    {
        glue_list *node = calloc(1, sizeof(*node));
        node->data = items[n];
        node->next = head;
        if (head) head->prev = node;
        head = node;
        lists_alive++;
    }
    return head;
}

static void *fake_display_get_default(void) { return no_display ? NULL : &display_obj; }
static void *fake_get_default_seat(void *d) { return d == &display_obj ? &seat_obj : NULL; }
static void *fake_get_device_manager(void *d) { return d == &display_obj ? &manager_obj : NULL; }

static glue_list *fake_list_devices(void *m, int type)
{
    CHECK(m == &manager_obj, "device manager passed through");
    CHECK_EQ_U(type, GLUE_DEVICE_TYPE_FLOATING);
    return make_list(floating, nfloating);
}

static glue_list *fake_seat_get_slaves(void *s, int caps)
{
    CHECK(s == &seat_obj, "seat passed through");
    return make_list(slaves, nslaves);
}

static void fake_list_free(glue_list *l)
{
    while (l) { glue_list *n = l->next; free(l); lists_alive--; l = n; }
}

static glue_list *fake_list_append(glue_list *l, void *data)
{
    glue_list *node = calloc(1, sizeof(*node)), *last = l;
    node->data = data;
    lists_alive++;
    if (!l) return node;
    while (last->next) last = last->next;
    last->next = node;
    node->prev = last;
    return l;
}

static void fake_signal_emit_by_name(void *instance, const char *signal, ...)
{
    va_list ap;
    CHECK(instance == &seat_obj, "signal emitted on the seat");
    emitted_signal = signal;
    va_start(ap, signal);
    if (nemitted < 16) emitted[nemitted++] = va_arg(ap, void *);
    va_end(ap);
}

static void fake_glue(struct gtk_glue *g)
{
    memset(g, 0, sizeof(*g));
    g->api.display_get_default = fake_display_get_default;
    g->api.display_get_default_seat = fake_get_default_seat;
    g->api.display_get_device_manager = fake_get_device_manager;
    g->api.device_manager_list_devices = fake_list_devices;
    g->api.seat_get_slaves = fake_seat_get_slaves;
    g->api.list_free = fake_list_free;
    g->api.list_append = fake_list_append;
    g->api.signal_emit_by_name = fake_signal_emit_by_name;
}

/* ---- tests ------------------------------------------------------------- */

/* floating Wintab devices get announced on the seat, once each */
void glue_announces_floating_devices(void)
{
    struct gtk_glue g;

    fake_glue(&g);
    floating[0] = &devices[0];
    nfloating = 1;
    CHECK_EQ_U(gtk_glue_scan(&g), 1);
    CHECK(emitted[0] == &devices[0], "device announced");
    CHECK(emitted_signal && !strcmp(emitted_signal, "device-added"), "signal name");

    CHECK_EQ_U(gtk_glue_scan(&g), 0);       /* no repeat */

    floating[1] = &devices[1];              /* a new stylus appears later */
    nfloating = 2;
    CHECK_EQ_U(gtk_glue_scan(&g), 1);
    CHECK(emitted[1] == &devices[1], "new device announced");
    CHECK_EQ_U(nemitted, 2);
    CHECK_EQ_U(lists_alive, 0);
}

/* devices already on the seat (a GTK with this fixed) are left alone */
void glue_skips_devices_on_seat(void)
{
    struct gtk_glue g;

    fake_glue(&g);
    floating[0] = &devices[0];
    floating[1] = &devices[1];
    nfloating = 2;
    slaves[0] = &devices[0];
    nslaves = 1;
    CHECK_EQ_U(gtk_glue_scan(&g), 1);
    CHECK_EQ_U(nemitted, 1);
    CHECK(emitted[0] == &devices[1], "only the unknown device announced");
    CHECK_EQ_U(lists_alive, 0);
}

/* the application's gdk_seat_get_slaves() also returns Wintab devices */
void glue_extends_seat_slaves(void)
{
    struct gtk_glue g;
    glue_list *l;
    int n = 0;

    fake_glue(&g);
    floating[0] = &devices[2];
    floating[1] = &devices[3];
    nfloating = 2;
    slaves[0] = &devices[0];            /* e.g. the system pointer */
    slaves[1] = &devices[3];            /* already present: not duplicated */
    nslaves = 2;

    l = gtk_glue_extend_slaves(&g, fake_seat_get_slaves(&seat_obj, GLUE_SEAT_CAPABILITY_ALL),
                               GLUE_SEAT_CAPABILITY_ALL);
    CHECK(l && l->data == &devices[0], "original order kept");
    for (glue_list *i = l; i; i = i->next) n++;
    CHECK_EQ_U(n, 3);
    CHECK(l && l->next && l->next->next && l->next->next->data == &devices[2], "missing device appended");
    fake_list_free(l);

    /* keyboard-only queries are left alone */
    l = gtk_glue_extend_slaves(&g, NULL, 8 /* GDK_SEAT_CAPABILITY_KEYBOARD */);
    CHECK(l == NULL, "keyboard query untouched");
    CHECK_EQ_U(nemitted, 0);
    CHECK_EQ_U(lists_alive, 0);
}

/* GTK points the system pointer's association at the newest Wintab
 * device; the application must see the seat's pointer instead */
static int pointer_obj, mouse_obj, pen_obj;
static void *fake_seat_get_pointer(void *s) { return s == &seat_obj ? &pointer_obj : NULL; }
static int fake_device_type(void *d)
{
    if (d == &pointer_obj) return 0;                    /* master */
    if (d == &mouse_obj) return GLUE_DEVICE_TYPE_SLAVE;
    return GLUE_DEVICE_TYPE_FLOATING;                   /* pens */
}

void glue_fixes_associated_device(void)
{
    struct gtk_glue g;

    fake_glue(&g);
    g.api.seat_get_pointer = fake_seat_get_pointer;
    g.api.device_get_device_type = fake_device_type;

    /* the bug: mouse -> pen. Fixed: mouse -> core pointer */
    CHECK(gtk_glue_fix_associated(&g, &mouse_obj, &pen_obj) == &pointer_obj, "mouse resolves to the pointer");
    /* correct associations pass through */
    CHECK(gtk_glue_fix_associated(&g, &mouse_obj, &pointer_obj) == &pointer_obj, "slave -> master kept");
    CHECK(gtk_glue_fix_associated(&g, &pointer_obj, &mouse_obj) == &mouse_obj, "master -> slave kept");
    CHECK(gtk_glue_fix_associated(&g, &pen_obj, NULL) == NULL, "no association kept");
    no_display = TRUE;
    CHECK(gtk_glue_fix_associated(&g, &mouse_obj, &pen_obj) == &pen_obj, "without a display, unchanged");
    no_display = FALSE;
}

/* ---- gdk_event_get_axis through the source device ---------------------- */

/* the start of a GdkEventMotion, as laid out on this platform */
struct fake_motion
{
    int type;
    void *window;
    signed char send_event;
    unsigned time;
    double x, y;
    double *axes;
};

static int real_axis_calls;
static size_t axes_alloc_len;       /* what _msize reports, in doubles */
static void *event_source;

static int fake_event_get_axis(const void *event, int use, double *value)
{
    real_axis_calls++;
    return 0;                       /* the core pointer has no such axis */
}
static void *fake_event_get_source_device(const void *e) { return event_source; }
/* the pen: x, y, pressure, xtilt, ytilt, wheel */
static int fake_axis_use(void *d, unsigned i) { static const int uses[] = { 1, 2, 3, 4, 5, 6 }; return uses[i]; }
static int fake_n_axes_pen(void *d) { return d == &pen_obj ? 6 : 2; }
static size_t fake_msize(void *p) { return axes_alloc_len * sizeof(double); }
static int state_calls;
static void *state_window;
static void fake_get_state(void *d, void *window, double *axes, int *mask)
{
    static const double latest[6] = { 11, 21, 0.42, 0.2, 0.1, 0.7 };
    state_calls++;
    state_window = window;
    memcpy(axes, latest, sizeof(latest));
}

static void axis_glue(struct gtk_glue *g)
{
    fake_glue(g);
    g->api.event_get_axis = fake_event_get_axis;
    g->api.event_get_source_device = fake_event_get_source_device;
    g->api.device_get_axis_use = fake_axis_use;
    g->api.device_get_n_axes = fake_n_axes_pen;
    g->api.device_get_device_type = fake_device_type;
    g->api.msize = fake_msize;
    g->api.device_get_state = fake_get_state;
}

void glue_event_axis_reads_through_the_source(void)
{
    struct gtk_glue g;
    double axes[6] = { 10, 20, 0.61, 0.1, -0.2, 0.5 };
    struct fake_motion ev = { 3 /* motion */ };
    double v = 0;

    CHECK(offsetof(struct fake_motion, axes) == GLUE_EVENT_AXES_OFFSET, "GdkEventMotion layout");
    axis_glue(&g);
    ev.axes = axes;
    event_source = &pen_obj;
    axes_alloc_len = 6;

    /* the core pointer has lost the pen's axes, but the event has them */
    CHECK(gtk_glue_event_axis(&g, &ev, 3 /* pressure */, &v) && v == 0.61, "pressure %f", v);
    CHECK(gtk_glue_event_axis(&g, &ev, 6 /* wheel */, &v) && v == 0.5, "wheel %f", v);
    CHECK_EQ_U(real_axis_calls, 0);

    ev.type = 4;                    /* button press: same layout */
    CHECK(gtk_glue_event_axis(&g, &ev, 4 /* xtilt */, &v) && v == 0.1, "xtilt on a press");
}

void glue_event_axis_respects_short_copies(void)
{
    struct gtk_glue g;
    double axes[2] = { 10, 20 };    /* copied while the core pointer had X, Y */
    static int window_obj;
    struct fake_motion ev = { 3 };
    double v = 0;

    axis_glue(&g);
    ev.axes = axes;
    ev.window = &window_obj;
    event_source = &pen_obj;
    axes_alloc_len = 2;

    /* nothing is read past the copy; the pen's latest state stands in */
    CHECK(gtk_glue_event_axis(&g, &ev, 3, &v) && v == 0.42, "pressure from the pen's state: %f", v);
    CHECK_EQ_U(state_calls, 1);
    CHECK(state_window == &window_obj, "state asked for the event's window");
    CHECK_EQ_U(real_axis_calls, 0);

    ev.window = NULL;               /* no window: leave it to GDK */
    CHECK(!gtk_glue_event_axis(&g, &ev, 3, &v), "falls back without a window");
    CHECK_EQ_U(real_axis_calls, 1);
}

void glue_event_axis_leaves_other_events_alone(void)
{
    struct gtk_glue g;
    double axes[6] = { 0 };
    struct fake_motion ev = { 3 };
    double v;

    axis_glue(&g);
    ev.axes = axes;
    axes_alloc_len = 6;

    event_source = &pen_obj;
    gtk_glue_event_axis(&g, &ev, 1 /* X: window coordinates */, &v);
    CHECK_EQ_U(real_axis_calls, 1);

    event_source = &mouse_obj;      /* a slave: the system mouse */
    gtk_glue_event_axis(&g, &ev, 3, &v);
    CHECK_EQ_U(real_axis_calls, 2);

    event_source = &pen_obj;
    ev.type = 10;                   /* enter-notify */
    gtk_glue_event_axis(&g, &ev, 3, &v);
    CHECK_EQ_U(real_axis_calls, 3);
}

/* import patching redirects calls made through the import table */
static DWORD WINAPI fake_GetCurrentProcessId(void) { return 0xC0FFEE; }

void iat_patch_own_import(void)
{
    DWORD real = GetCurrentProcessId();
    int n = iat_patch_module(GetModuleHandleW(NULL), "kernel32.dll", "GetCurrentProcessId",
                             (void *)fake_GetCurrentProcessId);
    CHECK_EQ_U(n, 1);
    CHECK_EQ_U(GetCurrentProcessId(), 0xC0FFEE);
    CHECK(real != 0xC0FFEE, "sanity");
    CHECK_EQ_U(iat_patch_module(GetModuleHandleW(NULL), "kernel32.dll", "NoSuchFunction",
                                (void *)fake_GetCurrentProcessId), 0);
    CHECK_EQ_U(iat_patch_module(GetModuleHandleW(NULL), "nosuch.dll", "GetCurrentProcessId",
                                (void *)fake_GetCurrentProcessId), 0);
}

/* nothing to do before GDK is up, or without devices */
void glue_tolerates_missing_display(void)
{
    struct gtk_glue g;

    fake_glue(&g);
    no_display = TRUE;
    CHECK_EQ_U(gtk_glue_scan(&g), 0);
    no_display = FALSE;
    nfloating = 0;
    CHECK_EQ_U(gtk_glue_scan(&g), 0);
    CHECK_EQ_U(nemitted, 0);
    CHECK_EQ_U(lists_alive, 0);
}
