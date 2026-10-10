/*
 * otd-wintab test harness. Tests run without tablet hardware: the harness
 * plays the producer (OpenTabletDriver) side of the shared ring and drives
 * the provider DLL through its public Wintab exports, like an application.
 *
 * Copyright 2026 otd-wintab contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef OTDWT_HARNESS_H
#define OTDWT_HARNESS_H

#include <stdio.h>
#include <string.h>

#include "wine_compat.h"
#include "wintab.h"
#include "ring.h"

/* ---- assertions -------------------------------------------------------- */

extern int test_failures;
#define CHECK(cond, ...) do { if (!(cond)) { test_failures++; \
    printf("    FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define CHECK_EQ_U(a, b) CHECK((a) == (b), "%s == %s: got %lu, expected %lu", #a, #b, \
                               (unsigned long)(a), (unsigned long)(b))
#define CHECK_EQ_S(a, b) CHECK(strcmp((a), (b)) == 0, "%s: got \"%s\", expected \"%s\"", #a, (a), (b))

/* ---- test registry ----------------------------------------------------- */

typedef void (*test_fn)(void);
struct test_case { const char *name; test_fn fn; BOOL needs_dll; };
extern const struct test_case test_cases[];
extern const int test_case_count;

/* ---- provider access --------------------------------------------------- */

struct wintab_api
{
    UINT (WINAPI *InfoA)(UINT, UINT, LPVOID);
    UINT (WINAPI *InfoW)(UINT, UINT, LPVOID);
    HCTX (WINAPI *OpenA)(HWND, LPLOGCONTEXTA, BOOL);
    BOOL (WINAPI *Close)(HCTX);
    BOOL (WINAPI *Packet)(HCTX, UINT, LPVOID);
    int  (WINAPI *PacketsGet)(HCTX, int, LPVOID);
    BOOL (WINAPI *Enable)(HCTX, BOOL);
    BOOL (WINAPI *QueueSizeSet)(HCTX, int);
    BOOL (WINAPI *GetA)(HCTX, LPLOGCONTEXTA);
};
extern struct wintab_api wt;
extern const char *dll_path;

/* Creates the ring (as the producer) and loads the provider. Tests that
 * need a different order call the two halves themselves. */
void harness_start(void);
void harness_create_ring(void);
void harness_load_dll(void);
void harness_stop(void);
struct ring_handle *harness_ring(void);

/* The packet layout requested by the harness contexts (GTK's set + status) */
#pragma pack(push, 1)
typedef struct
{
    HCTX ctx;
    UINT status;
    UINT serial;
    UINT cursor;
    DWORD buttons;
    DWORD x, y;
    UINT npressure;
    UINT tpressure;
    ORIENTATION orientation;
} PACKET;
#pragma pack(pop)
#define PACKETDATA (PK_CONTEXT | PK_STATUS | PK_SERIAL_NUMBER | PK_CURSOR | PK_BUTTONS | \
                    PK_X | PK_Y | PK_NORMAL_PRESSURE | PK_TANGENT_PRESSURE | PK_ORIENTATION)

/* Opens a context like GTK 3 (Y down, output = device axes). out_ext_x/y
 * override the output extents when non-zero. */
HCTX open_context(HWND hwnd, LONG out_ext_x, LONG out_ext_y, UINT device, int queue);
HWND make_window(void);

/* ---- producer helpers -------------------------------------------------- */

struct tool
{
    const char *name;
    uint32_t tool_id, serial, flags, axes;
};
extern const struct tool GRIP_PEN, GRIP_ERASER, AIRBRUSH, ART_PEN, NO_SERIAL_PEN;

void emit(uint32_t type, const struct tool *t, int x, int y, uint32_t pressure);
void emit_ex(const otdwt_event *ev);
void stroke(const struct tool *t, int npackets);  /* prox in, packets, prox out */
void settle(void);                                /* let the reader catch up */

/* ---- message pump ------------------------------------------------------ */

#define MAX_LOG 512
struct msg_log
{
    int csrchange, proximity_in, proximity_out, packet_msgs;
    int npackets;                   /* packets fetched with WTPacket */
    PACKET packets[MAX_LOG];
    char new_cursor_names[16][256]; /* names seen on WT_CSRCHANGE */
    int ncursor_names;
    /* GTK 3's proximity bookkeeping, simulated message by message
     * (gdkdevicemanager-win32.c): when ignore_core drops back to 0 GTK
     * makes the system pointer active again. */
    int gtk_entered_proximity, gtk_ignore_core;
};
/* Pumps messages for ms milliseconds, fetching packets like GTK does. */
void pump(struct msg_log *log, int ms);

#endif
