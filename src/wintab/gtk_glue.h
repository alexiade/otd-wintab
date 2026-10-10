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
};

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

/* Starts the periodic scan if this process uses GTK 3. Safe to call
 * from any thread, more than once. Disabled by OTD_WINTAB_NO_GTK_GLUE=1. */
void gtk_glue_start(void);

/* A new cursor's messages have just been posted: scan as soon as GTK has
 * handled them (an idle callback runs after pending window messages), so a
 * new stylus's first stroke isn't lost waiting for the periodic scan. Any
 * thread. */
void gtk_glue_kick(void);

#endif
