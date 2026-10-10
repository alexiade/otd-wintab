/*
 * otd-wintab: works around GTK 3 not announcing Wintab devices.
 *
 * GTK 3's Win32 backend creates Wintab devices as floating devices but
 * never adds them to the GdkSeat or emits "device-added" (gtk-3-24,
 * gdk/win32/gdkdevicemanager-win32.c, _wintab_recognize_new_cursors).
 * Applications that enumerate input devices through the seat, such as
 * GIMP 3, therefore never see them. When this provider is loaded into a
 * GTK 3 process it compensates in two ways:
 *
 *  - the application's (main executable's) import of gdk_seat_get_slaves
 *    is wrapped so the list also contains the floating Wintab devices, so
 *    a startup enumeration finds devices that already exist;
 *  - a GLib timer on the main loop emits the seat's "device-added" for each
 *    Wintab device once, so devices that appear later get picked up.
 *
 * GTK 3 also sets the *system pointer's* associated device to each new
 * Wintab device (instead of associating the Wintab device with the core
 * pointer). Applications that map a disabled mouse to its associated
 * device, as GIMP does to fall back to the core pointer, then attribute
 * plain mouse events to the most recently created pen. The application's
 * import of gdk_device_get_associated_device is wrapped so that a slave
 * device never resolves to a floating (Wintab) device, but to the seat's
 * pointer.
 *
 * And GTK 3 makes the *system pointer* the core pointer's active device
 * whenever it synthesizes crossing events from mouse messages, which
 * happens around grabs of child windows (GIMP's canvas) and as the cursor
 * moves between windows, even while a pen is in use. Pen events keep the
 * pen as source device, but the core pointer, their device, now only has X
 * and Y: GDK sizes copies of the events' axis arrays by it (events re-routed
 * to a grab window are copies) and gdk_event_get_axis() resolves through
 * it, so pressure, tilt and twist vanish while contact and motion remain.
 * GTK only re-selects the pen on a proximity-in, and the cursor keeps moving
 * (OpenTabletDriver moves it with the pen), so there's no keeping the core
 * pointer right from outside. Instead the application's import of
 * gdk_event_get_axis is wrapped: for Wintab-sourced pointer events,
 * non-positional axes are resolved through the source device. The event's
 * axis array is read only within its actual allocation (GLib allocates it
 * with the C runtime's malloc, so _msize knows); when a copy is short, the
 * pen's current state is used, which GTK keeps from the latest packet.
 *
 * Copyright 2026 otd-wintab contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef OTDWT_GTK_GLUE_H
#define OTDWT_GTK_GLUE_H

#include <windows.h>

/* The subset of GLib/GDK used, resolved at runtime (or faked in tests) */
typedef struct glue_list { void *data; struct glue_list *next, *prev; } glue_list;

struct gtk_api
{
    void      *(*display_get_default)(void);
    void      *(*display_get_default_seat)(void *display);
    void      *(*display_get_device_manager)(void *display);
    glue_list *(*device_manager_list_devices)(void *manager, int type);
    glue_list *(*seat_get_slaves)(void *seat, int capabilities);   /* the real one */
    void       (*list_free)(glue_list *list);
    glue_list *(*list_append)(glue_list *list, void *data);
    void      *(*seat_get_pointer)(void *seat);
    void      *(*device_get_associated_device)(void *device);   /* the real one */
    int        (*device_get_device_type)(void *device);
    void       (*signal_emit_by_name)(void *instance, const char *signal, ...);
    unsigned   (*timeout_add)(unsigned interval_ms, int (*fn)(void *), void *data);
    unsigned   (*idle_add)(int (*fn)(void *), void *data);
    int        (*device_get_n_axes)(void *device);
    int        (*event_get_axis)(const void *event, int use, double *value);  /* the real one */
    void      *(*event_get_source_device)(const void *event);
    int        (*device_get_axis_use)(void *device, unsigned index);
    size_t     (*msize)(void *block);      /* the C runtime GLib allocates with */
    void       (*device_get_state)(void *device, void *window, double *axes, int *mask);
};

/* GdkEvent layout bits used (GTK 3, stable ABI): pointer events start with
 * the type; GdkEventMotion and GdkEventButton keep the axes array right
 * after x and y. */
#define GLUE_EVENT_MOTION_NOTIFY    3
#define GLUE_EVENT_BUTTON_RELEASE   7       /* 4..7: press, 2/3-press, release */
#define GLUE_AXIS_X                 1
#define GLUE_AXIS_Y                 2
#define GLUE_EVENT_WINDOW_OFFSET    (sizeof(void *))
#define GLUE_EVENT_AXES_OFFSET      (sizeof(void *) == 8 ? 40 : 32)
#define GLUE_MAX_AXES               16

#define GLUE_DEVICE_TYPE_SLAVE      1   /* GDK_DEVICE_TYPE_SLAVE */
#define GLUE_DEVICE_TYPE_FLOATING   2   /* GDK_DEVICE_TYPE_FLOATING */
#define GLUE_SEAT_CAPABILITY_ALL    15  /* GDK_SEAT_CAPABILITY_ALL */
#define GLUE_SEAT_CAPABILITY_PENS   (1 | 4) /* POINTER | TABLET_STYLUS */
#define GLUE_MAX_ANNOUNCED          128

struct gtk_glue
{
    struct gtk_api api;
    void *announced[GLUE_MAX_ANNOUNCED];
    int nannounced;
};

/* Announces floating devices not yet on the seat. Returns how many. */
int gtk_glue_scan(struct gtk_glue *glue);

/* Appends floating devices missing from `slaves` (a gdk_seat_get_slaves
 * result for `capabilities`) and returns the new list head. */
glue_list *gtk_glue_extend_slaves(struct gtk_glue *glue, glue_list *slaves, int capabilities);

/* Given `device` and the associated device GTK reported for it, returns
 * the one the application should see (see above). */
void *gtk_glue_fix_associated(struct gtk_glue *glue, void *device, void *associated);

/* gdk_event_get_axis() resolving Wintab-sourced pointer events through
 * their source device (see above). */
int gtk_glue_event_axis(struct gtk_glue *glue, const void *event, int use, double *value);

/* Starts the periodic scan if this process uses GTK 3. Safe to call
 * from any thread, more than once. Disabled by OTD_WINTAB_NO_GTK_GLUE=1. */
void gtk_glue_start(void);

/* A new cursor's messages have just been posted: scan as soon as GTK has
 * handled them (an idle callback runs after pending window messages), so a
 * new stylus's first stroke isn't lost waiting for the periodic scan. Any
 * thread. */
void gtk_glue_kick(void);

#endif
