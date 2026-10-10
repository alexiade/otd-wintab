/*
 * otd-wintab: announce GTK 3 Wintab devices on the seat. See gtk_glue.h.
 *
 * Copyright 2026 otd-wintab contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <string.h>

#include "wine_compat.h"
#include "gtk_glue.h"
#include "iat.h"

static BOOL in_list(const glue_list *l, const void *p)
{
    for (; l; l = l->next)
        if (l->data == p) return TRUE;
    return FALSE;
}

static BOOL was_announced(const struct gtk_glue *glue, const void *device)
{
    int i;
    for (i = 0; i < glue->nannounced; i++)
        if (glue->announced[i] == device) return TRUE;
    return FALSE;
}

int gtk_glue_scan(struct gtk_glue *glue)
{
    const struct gtk_api *api = &glue->api;
    void *display, *seat, *manager;
    glue_list *floating, *slaves, *l;
    int count = 0;

    display = api->display_get_default();
    if (!display) return 0;
    seat = api->display_get_default_seat(display);
    manager = api->display_get_device_manager(display);
    if (!seat || !manager) return 0;

    floating = api->device_manager_list_devices(manager, GLUE_DEVICE_TYPE_FLOATING);
    if (!floating) return 0;
    slaves = api->seat_get_slaves(seat, GLUE_SEAT_CAPABILITY_ALL);

    for (l = floating; l; l = l->next)
    {
        /* already known to the seat (a fixed GTK) or already announced */
        if (in_list(slaves, l->data) || was_announced(glue, l->data))
            continue;
        if (glue->nannounced >= GLUE_MAX_ANNOUNCED)
            break;
        glue->announced[glue->nannounced++] = l->data;
        TRACE("announcing floating device %p on seat %p\n", l->data, seat);
        api->signal_emit_by_name(seat, "device-added", l->data);
        count++;
    }

    api->list_free(slaves);
    api->list_free(floating);
    return count;
}

glue_list *gtk_glue_extend_slaves(struct gtk_glue *glue, glue_list *slaves, int capabilities)
{
    const struct gtk_api *api = &glue->api;
    void *display, *manager;
    glue_list *floating, *l;

    if (!(capabilities & GLUE_SEAT_CAPABILITY_PENS)) return slaves;
    if (!(display = api->display_get_default())) return slaves;
    if (!(manager = api->display_get_device_manager(display))) return slaves;

    floating = api->device_manager_list_devices(manager, GLUE_DEVICE_TYPE_FLOATING);
    for (l = floating; l; l = l->next)
        if (!in_list(slaves, l->data))
            slaves = api->list_append(slaves, l->data);
    api->list_free(floating);
    return slaves;
}

void *gtk_glue_fix_associated(struct gtk_glue *glue, void *device, void *associated)
{
    const struct gtk_api *api = &glue->api;
    void *display, *seat;

    if (!device || !associated) return associated;
    if (api->device_get_device_type(device) != GLUE_DEVICE_TYPE_SLAVE ||
        api->device_get_device_type(associated) != GLUE_DEVICE_TYPE_FLOATING)
        return associated;
    if (!(display = api->display_get_default())) return associated;
    if (!(seat = api->display_get_default_seat(display))) return associated;
    return api->seat_get_pointer(seat);
}

int gtk_glue_event_axis(struct gtk_glue *glue, const void *event, int use, double *value)
{
    const struct gtk_api *api = &glue->api;
    void *source;
    double *axes;
    size_t len;
    int type, i, n;

    if (!event || use == GLUE_AXIS_X || use == GLUE_AXIS_Y)   /* window coordinates */
        return api->event_get_axis(event, use, value);

    type = *(const int *)event;
    if (type < GLUE_EVENT_MOTION_NOTIFY || type > GLUE_EVENT_BUTTON_RELEASE)
        return api->event_get_axis(event, use, value);

    source = api->event_get_source_device(event);
    if (!source || api->device_get_device_type(source) != GLUE_DEVICE_TYPE_FLOATING)
        return api->event_get_axis(event, use, value);

    axes = *(double *const *)((const char *)event + GLUE_EVENT_AXES_OFFSET);
    len = axes ? api->msize(axes) / sizeof(double) : 0;

    /* the array is laid out by the source (Wintab) device, possibly cut
     * short by a copy */
    n = api->device_get_n_axes(source);
    for (i = 0; i < n && i < GLUE_MAX_AXES; i++)
    {
        double state[GLUE_MAX_AXES];
        void *window;

        if (api->device_get_axis_use(source, i) != use) continue;
        if ((size_t)i < len)
        {
            if (value) *value = axes[i];
            return 1;
        }
        /* a short copy: the pen's current state, from its latest packet */
        window = *(void *const *)((const char *)event + GLUE_EVENT_WINDOW_OFFSET);
        if (!window) break;
        memset(state, 0, sizeof(state));
        api->device_get_state(source, window, state, NULL);
        if (value) *value = state[i];
        return 1;
    }
    return api->event_get_axis(event, use, value);
}

/* ---- runtime binding to the GTK in this process ----------------------- */

static struct gtk_glue the_glue;

static volatile LONG glue_active;

static int scan_tick(void *unused)
{
    gtk_glue_scan(&the_glue);
    return 1;   /* G_SOURCE_CONTINUE */
}

static int scan_once(void *unused)
{
    gtk_glue_scan(&the_glue);
    return 0;   /* G_SOURCE_REMOVE */
}

void gtk_glue_kick(void)
{
    if (glue_active)
        the_glue.api.idle_add(scan_once, NULL);
}

static glue_list *seat_get_slaves_hook(void *seat, int capabilities)
{
    glue_list *slaves = the_glue.api.seat_get_slaves(seat, capabilities);
    return gtk_glue_extend_slaves(&the_glue, slaves, capabilities);
}

static void *get_associated_device_hook(void *device)
{
    return gtk_glue_fix_associated(&the_glue, device,
                                   the_glue.api.device_get_associated_device(device));
}

static int event_get_axis_hook(const void *event, int use, double *value)
{
    return gtk_glue_event_axis(&the_glue, event, use, value);
}

/* _msize of the C runtime GLib's g_malloc uses: the one GLib imports
 * malloc from (the UCRT for current builds, msvcrt for old ones) */
static FARPROC crt_msize_for_glib(HMODULE glib)
{
    static const char *const crts[] = { "ucrtbase.dll", "api-ms-win-crt-heap-l1-1-0.dll", "msvcrt.dll" };
    BYTE *base = (BYTE *)glib;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *imp;
    size_t i;

    if (!dir->VirtualAddress) return NULL;
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress); imp->Name; imp++)
    {
        const char *name = (const char *)(base + imp->Name);
        for (i = 0; i < ARRAYSIZE(crts); i++)
        {
            HMODULE crt;
            if (_stricmp(name, crts[i])) continue;
            /* api sets resolve to ucrtbase */
            crt = GetModuleHandleA(i == 1 ? "ucrtbase.dll" : name);
            if (crt) return GetProcAddress(crt, "_msize");
        }
    }
    return NULL;
}

static BOOL bind(struct gtk_api *api)
{
    HMODULE gdk = GetModuleHandleA("libgdk-3-0.dll");
    HMODULE glib = GetModuleHandleA("libglib-2.0-0.dll");
    HMODULE gobject = GetModuleHandleA("libgobject-2.0-0.dll");

    if (!gdk || !glib || !gobject) return FALSE;
#define BIND(field, mod, name) if (!(*(FARPROC *)&api->field = GetProcAddress(mod, name))) return FALSE
    BIND(display_get_default, gdk, "gdk_display_get_default");
    BIND(display_get_default_seat, gdk, "gdk_display_get_default_seat");
    BIND(display_get_device_manager, gdk, "gdk_display_get_device_manager");
    BIND(device_manager_list_devices, gdk, "gdk_device_manager_list_devices");
    BIND(seat_get_slaves, gdk, "gdk_seat_get_slaves");
    BIND(seat_get_pointer, gdk, "gdk_seat_get_pointer");
    BIND(device_get_associated_device, gdk, "gdk_device_get_associated_device");
    BIND(device_get_device_type, gdk, "gdk_device_get_device_type");
    BIND(device_get_n_axes, gdk, "gdk_device_get_n_axes");
    BIND(event_get_axis, gdk, "gdk_event_get_axis");
    BIND(event_get_source_device, gdk, "gdk_event_get_source_device");
    BIND(device_get_axis_use, gdk, "gdk_device_get_axis_use");
    BIND(device_get_state, gdk, "gdk_device_get_state");
    if (!(*(FARPROC *)&api->msize = crt_msize_for_glib(glib))) return FALSE;
    BIND(list_free, glib, "g_list_free");
    BIND(list_append, glib, "g_list_append");
    BIND(timeout_add, glib, "g_timeout_add");
    BIND(idle_add, glib, "g_idle_add");
    BIND(signal_emit_by_name, gobject, "g_signal_emit_by_name");
#undef BIND
    return TRUE;
}

void gtk_glue_start(void)
{
    static LONG started;
    int wrapped;
    char v[8];
    DWORD n = GetEnvironmentVariableA("OTD_WINTAB_NO_GTK_GLUE", v, sizeof(v));

    if (n > 0 && n < sizeof(v) && v[0] != '0') return;
    if (InterlockedExchange(&started, 1)) return;
    if (!bind(&the_glue.api))
    {
        TRACE("not a GTK 3 process, glue inactive\n");
        return;
    }
    /* only the application's own view of the seat; GTK's internal calls
     * don't go through the executable's import table */
    wrapped = iat_patch_module(GetModuleHandleW(NULL), "libgdk-3-0.dll",
                               "gdk_seat_get_slaves", (void *)seat_get_slaves_hook);
    wrapped += iat_patch_module(GetModuleHandleW(NULL), "libgdk-3-0.dll",
                                "gdk_device_get_associated_device", (void *)get_associated_device_hook);
    wrapped += iat_patch_module(GetModuleHandleW(NULL), "libgdk-3-0.dll",
                                "gdk_event_get_axis", (void *)event_get_axis_hook);
    TRACE("GTK glue active, %d import(s) wrapped\n", wrapped);
    /* g_timeout_add/g_idle_add are thread-safe; callbacks run on the main loop */
    the_glue.api.timeout_add(250, scan_tick, NULL);
    InterlockedExchange(&glue_active, 1);
}
