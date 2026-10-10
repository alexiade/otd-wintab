/*
 * otd-wintab test cases. Each runs in its own process (see harness.c).
 *
 * Copyright 2026 otd-wintab contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <math.h>
#include <stdlib.h>

#include "harness.h"

static LONG ext_x(void) { AXIS a; wt.InfoA(WTI_DEVICES, DVC_X, &a); return a.axMax - a.axMin + 1; }
static LONG ext_y(void) { AXIS a; wt.InfoA(WTI_DEVICES, DVC_Y, &a); return a.axMax - a.axMin + 1; }

static UINT ncursors(void)
{
    UINT n = 0;
    wt.InfoA(WTI_INTERFACE, IFC_NCURSORS, &n);
    return n;
}

static const PACKET *find_packet(const struct msg_log *log, UINT x)
{
    int i;
    for (i = 0; i < log->npackets; i++)
        if (log->packets[i].x == x) return &log->packets[i];
    return NULL;
}

/* ======================================================================
 * WTInfo
 */

/* GTK's probe sequence must succeed before any tool has been seen */
static void info_probe_before_any_tool(void)
{
    UINT n = 99;
    WORD spec = 0;
    WCHAR name[256] = {0};
    LOGCONTEXTA lc;

    CHECK(wt.InfoA(0, 0, NULL) > 0, "WTInfo(0,0,NULL) must be non-zero or GTK disables Wintab");
    wt.InfoA(WTI_INTERFACE, IFC_SPECVERSION, &spec);
    CHECK_EQ_U(spec, 0x0101);
    wt.InfoA(WTI_INTERFACE, IFC_NDEVICES, &n);
    CHECK_EQ_U(n, 1);
    CHECK_EQ_U(ncursors(), 0);
    wt.InfoW(WTI_DEVICES, DVC_NAME, name);
    CHECK(!wcscmp(name, L"OpenTabletDriver"), "device name %ls", name);
    CHECK(wt.InfoA(WTI_DSCTXS, 0, &lc) == sizeof(LOGCONTEXTA), "device default context size");
    CHECK_EQ_U(lc.lcDevice, 0);
    CHECK_EQ_U(wt.InfoA(WTI_CURSORS, CSR_NAME, NULL), 0);     /* empty slot */
    CHECK_EQ_U(wt.InfoA(WTI_DEVICES + 1, DVC_NAME, NULL), 0); /* no such device */
}

/* ANSI entry points convert strings, including multi-strings */
static void info_ansi_strings(void)
{
    char buf[256];
    UINT size;
    HWND hwnd = make_window();
    struct msg_log log = {0};

    size = wt.InfoA(WTI_INTERFACE, IFC_WINTABID, NULL);
    CHECK(size == strlen("otd-wintab (OpenTabletDriver)") + 1, "WINTABID size %u", size);
    wt.InfoA(WTI_INTERFACE, IFC_WINTABID, buf);
    CHECK_EQ_S(buf, "otd-wintab (OpenTabletDriver)");
    wt.InfoA(WTI_DEVICES, DVC_NAME, buf);
    CHECK_EQ_S(buf, "OpenTabletDriver");

    open_context(hwnd, 0, 0, 0, 64);
    settle();
    stroke(&AIRBRUSH, 2);
    pump(&log, 300);

    wt.InfoA(WTI_CURSORS, CSR_NAME, buf);
    CHECK_EQ_S(buf, "Airbrush #0000B002");
    size = wt.InfoA(WTI_CURSORS, CSR_BTNNAMES, buf);
    CHECK(size == sizeof("Tip\0Lower barrel\0Upper barrel\0"), "BTNNAMES size %u", size);
    CHECK(!memcmp(buf, "Tip\0Lower barrel\0Upper barrel\0", size), "BTNNAMES content");
}

/* ======================================================================
 * dynamic cursors
 */

/* every new physical tool gets a cursor, announced with WT_CSRCHANGE */
static void cursors_created_per_tool(void)
{
    HWND hwnd = make_window();
    struct msg_log log = {0};

    open_context(hwnd, 0, 0, 0, 128);
    settle();
    stroke(&GRIP_PEN, 3);
    stroke(&AIRBRUSH, 3);
    stroke(&GRIP_ERASER, 3);
    stroke(&GRIP_PEN, 3);       /* known tool: reuses its cursor */
    pump(&log, 500);

    CHECK_EQ_U(ncursors(), 3);
    CHECK_EQ_U(log.csrchange, 4);
    CHECK_EQ_U(log.ncursor_names, 4);
    CHECK_EQ_S(log.new_cursor_names[0], "Grip Pen #0000A001");
    CHECK_EQ_S(log.new_cursor_names[1], "Airbrush #0000B002");
    CHECK_EQ_S(log.new_cursor_names[2], "Grip Pen Eraser #0000A001");
    CHECK_EQ_S(log.new_cursor_names[3], "Grip Pen #0000A001");
}

/* the same tool type with two serials is two devices */
static void cursors_distinguish_serials(void)
{
    struct tool second = GRIP_PEN;
    HWND hwnd = make_window();
    struct msg_log log = {0};
    char buf[256];

    second.serial = 0xA0FF;
    open_context(hwnd, 0, 0, 0, 128);
    settle();
    stroke(&GRIP_PEN, 1);
    stroke(&second, 1);
    pump(&log, 300);

    CHECK_EQ_U(ncursors(), 2);
    wt.InfoA(WTI_CURSORS + 1, CSR_NAME, buf);
    CHECK_EQ_S(buf, "Grip Pen #0000A0FF");
}

/* tablets without serials name the cursor by tool only */
static void cursors_without_serial(void)
{
    HWND hwnd = make_window();
    struct msg_log log = {0};
    char buf[256];

    open_context(hwnd, 0, 0, 0, 64);
    settle();
    stroke(&NO_SERIAL_PEN, 1);
    pump(&log, 300);

    wt.InfoA(WTI_CURSORS, CSR_NAME, buf);
    CHECK_EQ_S(buf, "Pen");
}

/* cursor attributes reflect the tool's capabilities */
static void cursors_attributes(void)
{
    HWND hwnd = make_window();
    struct msg_log log = {0};
    UINT pktdata, type, caps;
    DWORD physid;
    BOOL active;
    UINT ndev_csr = 0;

    open_context(hwnd, 0, 0, 0, 64);
    settle();
    stroke(&GRIP_PEN, 1);
    stroke(&AIRBRUSH, 1);
    stroke(&GRIP_ERASER, 1);
    pump(&log, 300);

    wt.InfoA(WTI_CURSORS + 0, CSR_PKTDATA, &pktdata);
    CHECK(!(pktdata & PK_TANGENT_PRESSURE), "grip pen must not report a wheel");
    CHECK(pktdata & PK_ORIENTATION, "grip pen reports tilt");
    wt.InfoA(WTI_CURSORS + 1, CSR_PKTDATA, &pktdata);
    CHECK(pktdata & PK_TANGENT_PRESSURE, "airbrush reports its wheel as tangential pressure");

    wt.InfoA(WTI_CURSORS + 1, CSR_TYPE, &type);
    CHECK_EQ_U(type, 0x913);
    wt.InfoA(WTI_CURSORS + 1, CSR_PHYSID, &physid);
    CHECK_EQ_U(physid, 0xB002);
    wt.InfoA(WTI_CURSORS + 1, CSR_ACTIVE, &active);
    CHECK(active, "cursor active");

    wt.InfoA(WTI_CURSORS + 2, CSR_CAPABILITIES, &caps);
    CHECK(caps & CRC_INVERT, "eraser cursor has CRC_INVERT");
    wt.InfoA(WTI_CURSORS + 0, CSR_CAPABILITIES, &caps);
    CHECK(!(caps & CRC_INVERT), "pen cursor lacks CRC_INVERT");

    wt.InfoA(WTI_DEVICES, DVC_NCSRTYPES, &ndev_csr);
    CHECK_EQ_U(ndev_csr, 3);
}

/* a proximity-out for a never-seen tool creates nothing */
static void cursors_unknown_proximity_out(void)
{
    HWND hwnd = make_window();
    struct msg_log log = {0};

    open_context(hwnd, 0, 0, 0, 64);
    settle();
    emit(OTDWT_EV_PROXIMITY_OUT, &GRIP_PEN, 10, 10, 0);
    pump(&log, 300);

    CHECK_EQ_U(ncursors(), 0);
    CHECK_EQ_U(log.proximity_out, 0);
}

/* running out of cursor slots degrades gracefully */
static void cursors_slot_limit(void)
{
    HWND hwnd = make_window();
    struct msg_log log = {0};
    struct tool t = GRIP_PEN;
    int i;

    open_context(hwnd, 0, 0, 0, 128);
    settle();
    for (i = 0; i < 105; i++)
    {
        t.serial = 0x1000 + i;
        emit(OTDWT_EV_PROXIMITY_IN, &t, 10, 10, 0);
        emit(OTDWT_EV_PROXIMITY_OUT, &t, 10, 10, 0);
        if (i % 20 == 19) pump(&log, 50);
    }
    pump(&log, 300);
    CHECK_EQ_U(ncursors(), 100);
}

/* ======================================================================
 * packet content
 */

static void packets_pressure_buttons_wheel(void)
{
    HWND hwnd = make_window();
    struct msg_log log = {0};
    const PACKET *p;

    open_context(hwnd, 0, 0, 0, 128);
    settle();
    stroke(&AIRBRUSH, 4);
    pump(&log, 300);

    p = find_packet(&log, 104 * OTDWT_POS_SCALE);
    CHECK(p != NULL, "last stroke packet delivered");
    if (!p) return;
    CHECK_EQ_U(p->npressure, 16000);
    CHECK_EQ_U(p->tpressure, 700);
    CHECK_EQ_U(p->buttons & 1, 1);
    CHECK_EQ_U(p->status & TPS_INVERT, 0);
}

static void packets_tilt_to_orientation(void)
{
    HWND hwnd = make_window();
    struct msg_log log = {0};
    const PACKET *p;
    /* tilt (20.0deg, -10.0deg) as emitted by the harness */
    int azimuth = (int)(0.5 + (atan2(-100.0, 200.0) + M_PI_2) * 1800.0 / M_PI);
    int altitude = 900 - (int)sqrt(200.0 * 200.0 + 100.0 * 100.0);

    open_context(hwnd, 0, 0, 0, 128);
    settle();
    stroke(&GRIP_PEN, 2);
    pump(&log, 300);

    p = find_packet(&log, 102 * OTDWT_POS_SCALE);
    CHECK(p != NULL, "packet delivered");
    if (!p) return;
    CHECK(abs(p->orientation.orAzimuth - azimuth) <= 1, "azimuth %d, expected %d", p->orientation.orAzimuth, azimuth);
    CHECK(abs(p->orientation.orAltitude - altitude) <= 1, "altitude %d, expected %d", p->orientation.orAltitude, altitude);
    CHECK_EQ_U(p->orientation.orTwist, 0);  /* grip pen has no rotation */
}

static void packets_rotation_to_twist(void)
{
    HWND hwnd = make_window();
    struct msg_log log = {0};
    const PACKET *p;

    open_context(hwnd, 0, 0, 0, 128);
    settle();
    stroke(&ART_PEN, 2);
    pump(&log, 300);

    p = find_packet(&log, 102 * OTDWT_POS_SCALE);
    CHECK(p != NULL, "packet delivered");
    if (p) CHECK_EQ_U(p->orientation.orTwist, 450);
}

static void packets_eraser_inverted(void)
{
    HWND hwnd = make_window();
    struct msg_log log = {0};
    const PACKET *p;

    open_context(hwnd, 0, 0, 0, 128);
    settle();
    stroke(&GRIP_ERASER, 2);
    pump(&log, 300);

    p = find_packet(&log, 102 * OTDWT_POS_SCALE);
    CHECK(p != NULL, "packet delivered");
    if (!p) return;
    CHECK(p->status & TPS_INVERT, "eraser packets carry TPS_INVERT");
    CHECK(p->orientation.orAltitude < 0, "eraser altitude is negative (Wintab convention)");
    CHECK_EQ_U(p->npressure, 8000);
}

static void packets_serials_increase(void)
{
    HWND hwnd = make_window();
    struct msg_log log = {0};
    int i;

    open_context(hwnd, 0, 0, 0, 128);
    settle();
    stroke(&GRIP_PEN, 10);
    pump(&log, 300);

    CHECK(log.npackets >= 10, "packets %d", log.npackets);
    for (i = 1; i < log.npackets; i++)
        CHECK(log.packets[i].serial > log.packets[i - 1].serial, "serial order at %d", i);
}

/* ======================================================================
 * proximity
 */

static void proximity_messages(void)
{
    HWND hwnd = make_window();
    struct msg_log log = {0};

    open_context(hwnd, 0, 0, 0, 128);
    settle();
    stroke(&GRIP_PEN, 2);
    stroke(&AIRBRUSH, 2);
    pump(&log, 300);

    CHECK_EQ_U(log.proximity_in, 2);
    CHECK_EQ_U(log.proximity_out, 2);
}

/* after the pen leaves, GTK must hand control back to the mouse: its
 * ignore_core count has to return to zero for new and known tools alike */
static void proximity_returns_to_system_pointer(void)
{
    HWND hwnd = make_window();
    struct msg_log log = {0};

    open_context(hwnd, 0, 0, 0, 128);
    settle();
    stroke(&GRIP_PEN, 3);       /* new tool */
    pump(&log, 200);
    CHECK(log.gtk_ignore_core == 0, "after a new tool: ignore_core %d", log.gtk_ignore_core);
    stroke(&GRIP_PEN, 3);       /* same tool again */
    pump(&log, 200);
    CHECK(log.gtk_ignore_core == 0, "after the same tool: ignore_core %d", log.gtk_ignore_core);
    stroke(&AIRBRUSH, 3);       /* switching tools */
    stroke(&GRIP_ERASER, 3);
    pump(&log, 200);
    CHECK(log.gtk_ignore_core == 0, "after switching tools: ignore_core %d", log.gtk_ignore_core);
    CHECK_EQ_U(log.gtk_entered_proximity, 0);
}

/* a tool that leaves with its tip still down gets a release first, or the
 * application keeps its stroke grab and ignores every other device */
static void proximity_out_releases_buttons(void)
{
    HWND hwnd = make_window();
    struct msg_log log = {0};
    int i;

    open_context(hwnd, 0, 0, 0, 128);
    settle();
    emit(OTDWT_EV_PROXIMITY_IN, &GRIP_PEN, 10, 10, 0);
    emit(OTDWT_EV_PACKET, &GRIP_PEN, 11, 11, 5000);     /* tip down */
    emit(OTDWT_EV_PROXIMITY_OUT, &GRIP_PEN, 11, 11, 0); /* lifted, no release packet */
    pump(&log, 300);

    CHECK(log.npackets >= 2, "packets %d", log.npackets);
    for (i = 0; i < log.npackets; i++)
        if (log.packets[i].buttons & 1) break;
    CHECK(i < log.npackets, "tip-down packet seen");
    CHECK(log.npackets > 0 && log.packets[log.npackets - 1].buttons == 0,
          "last packet before leaving releases the tip (buttons %#lx)",
          log.npackets ? log.packets[log.npackets - 1].buttons : 0);
    CHECK(log.npackets > 0 && log.packets[log.npackets - 1].npressure == 0, "and has no pressure");
}

/* attaching mid-stroke synthesises the missing proximity-in */
static void proximity_synthesised_mid_stroke(void)
{
    HWND hwnd = make_window();
    struct msg_log log = {0};

    open_context(hwnd, 0, 0, 0, 128);
    settle();
    emit(OTDWT_EV_PACKET, &GRIP_PEN, 50, 50, 1000);
    emit(OTDWT_EV_PACKET, &GRIP_PEN, 51, 51, 1000);
    pump(&log, 300);

    CHECK_EQ_U(log.proximity_in, 1);
    CHECK_EQ_U(log.csrchange, 1);
    CHECK_EQ_U(ncursors(), 1);
}

/* ======================================================================
 * coordinates and context mapping
 */

/* GTK asks for Y growing downward with output = device axes: identity */
static void coords_gtk_identity(void)
{
    HWND hwnd = make_window();
    struct msg_log log = {0};
    const PACKET *p;

    open_context(hwnd, 0, 0, 0, 128);
    settle();
    stroke(&GRIP_PEN, 3);
    pump(&log, 300);

    p = find_packet(&log, 103 * OTDWT_POS_SCALE);
    CHECK(p != NULL, "packet at x=103px");
    if (p) CHECK_EQ_U(p->y, 203 * OTDWT_POS_SCALE);
}

/* a classic Wintab context (positive Y extent) gets Y growing upward */
static void coords_classic_y_up(void)
{
    HWND hwnd = make_window();
    struct msg_log log = {0};
    const PACKET *p;
    LONG h;

    open_context(hwnd, 0, ext_y(), 0, 128);
    h = ext_y();
    settle();
    stroke(&GRIP_PEN, 3);
    pump(&log, 300);

    p = find_packet(&log, 103 * OTDWT_POS_SCALE);
    CHECK(p != NULL, "packet at x=103px");
    if (p) CHECK_EQ_U(p->y, h - 203 * OTDWT_POS_SCALE);
}

/* output extents scale the coordinates */
static void coords_scaled_context(void)
{
    HWND hwnd = make_window();
    struct msg_log log = {0};
    LONG ex = ext_x(), ey = ext_y();
    int i;
    BOOL found = FALSE;

    open_context(hwnd, 10000, -10000, 0, 128);
    settle();
    stroke(&GRIP_PEN, 3);
    pump(&log, 300);

    for (i = 0; i < log.npackets; i++)
        if (log.packets[i].x == (DWORD)MulDiv(103 * OTDWT_POS_SCALE, 10000, ex))
        {
            found = TRUE;
            CHECK_EQ_U(log.packets[i].y, (DWORD)MulDiv(203 * OTDWT_POS_SCALE, 10000, ey));
        }
    CHECK(found, "scaled packet found");
}

/* ======================================================================
 * contexts: routing, enable, queue
 */

static void context_every_open_context_receives(void)
{
    HWND a = make_window(), b = make_window();
    struct msg_log log = {0};
    HCTX ca, cb;
    int i, from_a = 0, from_b = 0;

    ca = open_context(a, 0, 0, 0, 128);
    cb = open_context(b, 0, 0, 0, 128);
    settle();
    stroke(&GRIP_PEN, 3);
    pump(&log, 300);

    for (i = 0; i < log.npackets; i++)
    {
        if (log.packets[i].ctx == ca) from_a++;
        if (log.packets[i].ctx == cb) from_b++;
    }
    CHECK(from_a >= 3 && from_b >= 3, "both contexts receive (a=%d b=%d)", from_a, from_b);
}

static void context_other_device_receives_nothing(void)
{
    HWND hwnd = make_window();
    struct msg_log log = {0};

    open_context(hwnd, 0, 0, 5, 128);
    settle();
    stroke(&GRIP_PEN, 3);
    pump(&log, 300);

    CHECK_EQ_U(log.npackets, 0);
    CHECK_EQ_U(log.proximity_in, 0);
}

static void context_disable_enable(void)
{
    HWND hwnd = make_window();
    struct msg_log log = {0};
    HCTX h;

    h = open_context(hwnd, 0, 0, 0, 128);
    wt.Enable(h, FALSE);
    settle();
    stroke(&GRIP_PEN, 3);
    pump(&log, 300);
    CHECK_EQ_U(log.npackets, 0);

    wt.Enable(h, TRUE);
    stroke(&GRIP_PEN, 3);
    pump(&log, 300);
    CHECK(log.npackets >= 3, "packets after re-enable: %d", log.npackets);
}

/* a full queue drops new packets and flags the last kept one */
static void context_queue_overflow(void)
{
    HWND hwnd = make_window();
    PACKET buf[64];
    HCTX h;
    int n;

    h = open_context(hwnd, 0, 0, 0, 8);
    settle();
    stroke(&GRIP_PEN, 20);          /* no pumping: the queue fills up */
    settle();

    n = wt.PacketsGet(h, 64, buf);
    CHECK_EQ_U(n, 8);
    if (n == 8) CHECK(buf[7].status & TPS_QUEUE_ERR, "overflow flagged");
}

/* WTPacketsGet drains in order and in pieces */
static void context_packets_get_in_order(void)
{
    HWND hwnd = make_window();
    PACKET buf[64];
    HCTX h;
    int n1, n2, i;

    h = open_context(hwnd, 0, 0, 0, 64);
    settle();
    stroke(&GRIP_PEN, 10);          /* 13 packets: proximity, 10, tip up, proximity */
    settle();

    /* cMaxPkts is the buffer capacity: the provider may clear all of it */
    n1 = wt.PacketsGet(h, 5, buf);
    n2 = wt.PacketsGet(h, ARRAYSIZE(buf) - 5, buf + 5);
    CHECK_EQ_U(n1, 5);
    CHECK_EQ_U(n2, 8);
    for (i = 1; i < n1 + n2; i++)
        CHECK(buf[i].serial == buf[i - 1].serial + 1, "serial continuity at %d", i);
    CHECK_EQ_U(wt.PacketsGet(h, 64, buf), 0);
}

/* ======================================================================
 * ring attachment
 */

/* events published before the provider starts are history, not input */
static void ring_stale_history_ignored(void)
{
    HWND hwnd;
    struct msg_log log = {0};

    harness_create_ring();
    stroke(&GRIP_PEN, 5);
    harness_load_dll();
    hwnd = make_window();
    open_context(hwnd, 0, 0, 0, 64);
    pump(&log, 300);

    CHECK_EQ_U(log.npackets, 0);
    CHECK_EQ_U(ncursors(), 0);
}

/* the provider attaches when the producer (OpenTabletDriver) starts later */
static void ring_producer_starts_late(void)
{
    HWND hwnd;
    struct msg_log log = {0};

    harness_load_dll();
    hwnd = make_window();
    open_context(hwnd, 0, 0, 0, 64);
    pump(&log, 200);
    harness_create_ring();
    pump(&log, 800);                /* provider retries every 500 ms */
    stroke(&GRIP_PEN, 3);
    pump(&log, 300);

    CHECK(log.npackets >= 3, "packets after late attach: %d", log.npackets);
}

/* ---- ring unit tests (no provider) ------------------------------------ */

static void ring_unit_publish_read(void)
{
    struct ring_handle *r = harness_ring();
    struct ring_handle reader;
    otdwt_event ev;
    uint64_t idx = 0;
    int i;

    harness_create_ring();
    CHECK(ring_open(&reader), "reader opens the ring");
    for (i = 0; i < 10; i++) { emit(OTDWT_EV_PACKET, &GRIP_PEN, i, i, 0); }
    for (i = 0; i < 10; i++)
    {
        CHECK(ring_read(&reader, &idx, &ev) == 1, "event %d available", i);
        CHECK_EQ_U(ev.x, (uint32_t)(i * OTDWT_POS_SCALE));
    }
    CHECK(ring_read(&reader, &idx, &ev) == 0, "ring drained");
    CHECK_EQ_U(r->ring->write_index, 10);
    ring_close(&reader);
}

/* a reader that falls more than a ring behind skips to the oldest event
 * still present, rather than reading overwritten slots */
static void ring_unit_overrun(void)
{
    struct ring_handle reader;
    otdwt_event ev;
    uint64_t idx = 0;
    int i, n = 0;

    harness_create_ring();
    ring_open(&reader);
    for (i = 0; i < 3000; i++) emit(OTDWT_EV_PACKET, &GRIP_PEN, i, 0, 0);
    CHECK(ring_read(&reader, &idx, &ev) == 1, "first read");
    CHECK_EQ_U(ev.x, (uint32_t)((3000 - OTDWT_RING_SIZE) * OTDWT_POS_SCALE));
    n = 1;
    while (ring_read(&reader, &idx, &ev)) n++;
    CHECK_EQ_U(n, OTDWT_RING_SIZE);
    CHECK_EQ_U(ev.x, 2999 * OTDWT_POS_SCALE);
    ring_close(&reader);
}

/* a restarted producer resets write_index; readers resynchronise */
static void ring_unit_producer_restart(void)
{
    struct ring_handle *r = harness_ring();
    struct ring_handle reader;
    otdwt_event ev;
    uint64_t idx = 0;
    int i;

    harness_create_ring();
    ring_open(&reader);
    for (i = 0; i < 50; i++) emit(OTDWT_EV_PACKET, &GRIP_PEN, i, 0, 0);
    while (ring_read(&reader, &idx, &ev)) {}
    CHECK_EQ_U(idx, 50);

    r->ring->write_index = 0;       /* new producer process */
    emit(OTDWT_EV_PACKET, &GRIP_PEN, 777, 0, 0);
    CHECK(ring_read(&reader, &idx, &ev) == 0, "resync consumes nothing stale");
    emit(OTDWT_EV_PACKET, &GRIP_PEN, 778, 0, 0);
    CHECK(ring_read(&reader, &idx, &ev) == 1, "new event after restart");
    CHECK_EQ_U(ev.x, 778 * OTDWT_POS_SCALE);
    ring_close(&reader);
}

/* a ring with a different layout version is refused */
static void ring_unit_version_mismatch(void)
{
    struct ring_handle *r = harness_ring();
    struct ring_handle reader;

    harness_create_ring();
    r->ring->version = OTDWT_VERSION + 1;
    CHECK(!ring_open(&reader), "mismatched version refused");
    r->ring->version = OTDWT_VERSION;
    CHECK(ring_open(&reader), "matching version accepted");
    ring_close(&reader);
}

/* ====================================================================== */

/* test_gtk_glue.c */
void glue_announces_floating_devices(void);
void glue_skips_devices_on_seat(void);
void glue_tolerates_missing_display(void);
void glue_extends_seat_slaves(void);
void glue_fixes_associated_device(void);
void glue_event_axis_reads_through_the_source(void);
void glue_event_axis_respects_short_copies(void);
void glue_event_axis_leaves_other_events_alone(void);
void iat_patch_own_import(void);

#define T(fn, dll) { #fn, fn, dll }
const struct test_case test_cases[] =
{
    T(info_probe_before_any_tool, TRUE),
    T(info_ansi_strings, TRUE),
    T(cursors_created_per_tool, TRUE),
    T(cursors_distinguish_serials, TRUE),
    T(cursors_without_serial, TRUE),
    T(cursors_attributes, TRUE),
    T(cursors_unknown_proximity_out, TRUE),
    T(cursors_slot_limit, TRUE),
    T(packets_pressure_buttons_wheel, TRUE),
    T(packets_tilt_to_orientation, TRUE),
    T(packets_rotation_to_twist, TRUE),
    T(packets_eraser_inverted, TRUE),
    T(packets_serials_increase, TRUE),
    T(proximity_messages, TRUE),
    T(proximity_returns_to_system_pointer, TRUE),
    T(proximity_out_releases_buttons, TRUE),
    T(proximity_synthesised_mid_stroke, TRUE),
    T(coords_gtk_identity, TRUE),
    T(coords_classic_y_up, TRUE),
    T(coords_scaled_context, TRUE),
    T(context_every_open_context_receives, TRUE),
    T(context_other_device_receives_nothing, TRUE),
    T(context_disable_enable, TRUE),
    T(context_queue_overflow, TRUE),
    T(context_packets_get_in_order, TRUE),
    T(ring_stale_history_ignored, FALSE),
    T(ring_producer_starts_late, FALSE),
    T(ring_unit_publish_read, FALSE),
    T(ring_unit_overrun, FALSE),
    T(ring_unit_producer_restart, FALSE),
    T(ring_unit_version_mismatch, FALSE),
    T(glue_announces_floating_devices, FALSE),
    T(glue_skips_devices_on_seat, FALSE),
    T(glue_tolerates_missing_display, FALSE),
    T(glue_extends_seat_slaves, FALSE),
    T(glue_fixes_associated_device, FALSE),
    T(glue_event_axis_reads_through_the_source, FALSE),
    T(glue_event_axis_respects_short_copies, FALSE),
    T(glue_event_axis_leaves_other_events_alone, FALSE),
    T(iat_patch_own_import, FALSE),
};
const int test_case_count = ARRAYSIZE(test_cases);
