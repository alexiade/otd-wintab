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
