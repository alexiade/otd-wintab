/*
 * otd-wintab: Wintab backend fed by the shared event ring.
 *
 * The device/cursor model and the WTInfo answers follow Wine's X11 tablet
 * driver (dlls/winex11.drv/wintab.c, Copyright 2003 CodeWeavers, Aric
 * Stewart). Unlike Wine, cursors are created dynamically: the first time a
 * physical tool (tool id + serial) shows up, it gets its own cursor, named
 * after the tool and its serial, and applications that watch WT_CSRCHANGE
 * (GTK, hence GIMP) pick it up as a new input device.
 *
 * Copyright 2003 CodeWeavers, Aric Stewart
 * Copyright 2026 otd-wintab contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "wintab.h"
#include "wintab_internal.h"
#include "backend.h"
#include "ring.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wintab32);

#define WT_MAX_NAME_LEN 256
#define CURSORMAX       100     /* WTI_CURSORS .. WTI_CURSORS+99 */

/* From Wine's winex11.drv/wintab.c */
typedef struct tagWTI_CURSORS_INFO
{
    WCHAR   NAME[WT_MAX_NAME_LEN];
    BOOL    ACTIVE;
    WTPKT   PKTDATA;
    BYTE    BUTTONS;
    BYTE    BUTTONBITS;
    DWORD   cchBTNNAMES;
    WCHAR   BTNNAMES[64];
    BYTE    BUTTONMAP[32];
    BYTE    SYSBTNMAP[32];
    BYTE    NPBUTTON;
    UINT    NPBTNMARKS[2];
    BYTE    TPBUTTON;
    UINT    TPBTNMARKS[2];
    DWORD   PHYSID;
    UINT    MODE;
    UINT    MINPKTDATA;
    UINT    MINBUTTONS;
    UINT    CAPABILITIES;
    UINT    TYPE;
    /* otd-wintab */
    BOOL    in_proximity;
} WTI_CURSORS_INFO, *LPWTI_CURSORS_INFO;

typedef struct tagWTI_DEVICES_INFO
{
    WCHAR   NAME[WT_MAX_NAME_LEN];
    UINT    HARDWARE;
    UINT    NCSRTYPES;
    UINT    FIRSTCSR;
    UINT    PKTRATE;
    WTPKT   PKTDATA;
    WTPKT   PKTMODE;
    WTPKT   CSRDATA;
    INT     XMARGIN;
    INT     YMARGIN;
    INT     ZMARGIN;
    AXIS    X;
    AXIS    Y;
    AXIS    Z;
    AXIS    NPRESSURE;
    AXIS    TPRESSURE;
    AXIS    ORIENTATION[3];
    AXIS    ROTATION[3];
    WCHAR   PNPID[WT_MAX_NAME_LEN];
} WTI_DEVICES_INFO, *LPWTI_DEVICES_INFO;

#define BASE_PKTDATA (PK_CONTEXT | PK_STATUS | PK_TIME | PK_CHANGED | PK_SERIAL_NUMBER | \
                      PK_CURSOR | PK_BUTTONS | PK_X | PK_Y | PK_NORMAL_PRESSURE | PK_ORIENTATION)

/* Our device's index among all Wintab devices. 0 while we are the only
 * provider; a pass-through layer will place real devices before us. */
static UINT our_device = 0;

static LOGCONTEXTW      gSysContext;
static WTI_DEVICES_INFO gSysDevice;
static WTI_CURSORS_INFO gSysCursor[CURSORMAX];
static UINT             gNumCursors;
static SRWLOCK          cursor_lock = SRWLOCK_INIT;

static HANDLE reader_thread;
static volatile LONG stopping;
static UINT serial_counter;
static WTPACKET last_packet;

DWORD WINAPI fake_source_thread(void *arg);
BOOL fake_source_enabled(void);

/***********************************************************************
 * device / context setup (after Wine's load_tablet_info)
 */
static void setup_device(void)
{
    LONG vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    LONG vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    LONG ext_x = vw * OTDWT_POS_SCALE, ext_y = vh * OTDWT_POS_SCALE;

    wcscpy(gSysContext.lcName, L"OpenTabletDriver Context");
    gSysContext.lcOptions = CXO_SYSTEM;
    gSysContext.lcLocks = CXL_INSIZE | CXL_INASPECT | CXL_MARGIN | CXL_SENSITIVITY | CXL_SYSOUT;
    gSysContext.lcMsgBase = WT_DEFBASE;
    gSysContext.lcDevice = our_device;
    gSysContext.lcPktData = BASE_PKTDATA | PK_TANGENT_PRESSURE;
    gSysContext.lcMoveMask = PK_BUTTONS | PK_X | PK_Y | PK_NORMAL_PRESSURE |
                             PK_TANGENT_PRESSURE | PK_ORIENTATION;
    gSysContext.lcStatus = CXS_ONTOP;
    gSysContext.lcPktRate = 200;
    gSysContext.lcBtnDnMask = 0xffffffff;
    gSysContext.lcBtnUpMask = 0xffffffff;
    gSysContext.lcInOrgX = 0;
    gSysContext.lcInOrgY = 0;
    gSysContext.lcInExtX = ext_x;
    gSysContext.lcInExtY = ext_y;
    gSysContext.lcOutOrgX = 0;
    gSysContext.lcOutOrgY = 0;
    gSysContext.lcOutExtX = ext_x;
    gSysContext.lcOutExtY = ext_y;
    gSysContext.lcSensX = 65536;
    gSysContext.lcSensY = 65536;
    gSysContext.lcSensZ = 65536;
    gSysContext.lcSysMode = 0;
    gSysContext.lcSysOrgX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    gSysContext.lcSysOrgY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    gSysContext.lcSysExtX = vw;
    gSysContext.lcSysExtY = vh;
    gSysContext.lcSysSensX = 65536;
    gSysContext.lcSysSensY = 65536;

    wcscpy(gSysDevice.NAME, L"OpenTabletDriver");
    wcscpy(gSysDevice.PNPID, L"non-plug-n-play");
    gSysDevice.HARDWARE = HWC_HARDPROX | HWC_PHYSID_CURSORS;
    gSysDevice.FIRSTCSR = 0;
    gSysDevice.PKTRATE = 200;
    gSysDevice.PKTDATA = BASE_PKTDATA;
    gSysDevice.CSRDATA = PK_TANGENT_PRESSURE;

    gSysDevice.X.axMin = 0;
    gSysDevice.X.axMax = ext_x - 1;
    gSysDevice.X.axUnits = TU_INCHES;
    gSysDevice.X.axResolution = CASTFIX32(96 * OTDWT_POS_SCALE);
    gSysDevice.Y = gSysDevice.X;
    gSysDevice.Y.axMax = ext_y - 1;

    gSysDevice.NPRESSURE.axMin = 0;
    gSysDevice.NPRESSURE.axMax = OTDWT_PRESSURE_MAX;
    gSysDevice.NPRESSURE.axUnits = TU_NONE;
    gSysDevice.NPRESSURE.axResolution = 0;

    gSysDevice.TPRESSURE.axMin = 0;
    gSysDevice.TPRESSURE.axMax = OTDWT_WHEEL_MAX;
    gSysDevice.TPRESSURE.axUnits = TU_NONE;
    gSysDevice.TPRESSURE.axResolution = 0;

    /* azimuth, altitude, twist; as Wine, in tenths of a degree */
    gSysDevice.ORIENTATION[0].axMin = 0;
    gSysDevice.ORIENTATION[0].axMax = 3600;
    gSysDevice.ORIENTATION[0].axUnits = TU_CIRCLE;
    gSysDevice.ORIENTATION[0].axResolution = CASTFIX32(3600);
    gSysDevice.ORIENTATION[1].axMin = -1000;
    gSysDevice.ORIENTATION[1].axMax = 1000;
    gSysDevice.ORIENTATION[1].axUnits = TU_CIRCLE;
    gSysDevice.ORIENTATION[1].axResolution = CASTFIX32(3600);
    gSysDevice.ORIENTATION[2] = gSysDevice.ORIENTATION[0];
}

/***********************************************************************
 * dynamic cursors
 */
static int find_cursor(const otdwt_event *ev)
{
    UINT i;
    for (i = 0; i < gNumCursors; i++)
        if (gSysCursor[i].TYPE == ev->tool_id && gSysCursor[i].PHYSID == ev->serial &&
            !!(gSysCursor[i].CAPABILITIES & CRC_INVERT) == !!(ev->flags & OTDWT_F_ERASER))
            return i;
    return -1;
}

static int add_cursor(const otdwt_event *ev)
{
    LPWTI_CURSORS_INFO c;
    char name[OTDWT_NAME_LEN + 16];
    char tool[OTDWT_NAME_LEN];
    static const WCHAR btnnames[] = L"Tip\0Lower barrel\0Upper barrel\0";
    int i;

    if (gNumCursors >= CURSORMAX)
    {
        ERR("out of cursor slots\n");
        return -1;
    }

    memcpy(tool, ev->name, OTDWT_NAME_LEN);
    tool[OTDWT_NAME_LEN - 1] = 0;
    if (!tool[0]) strcpy(tool, (ev->flags & OTDWT_F_ERASER) ? "Eraser" : "Pen");
    if (ev->serial)
        snprintf(name, sizeof(name), "%s #%08X", tool, ev->serial);
    else
        snprintf(name, sizeof(name), "%s", tool);

    c = &gSysCursor[gNumCursors];
    memset(c, 0, sizeof(*c));
    MultiByteToWideChar(CP_UTF8, 0, name, -1, c->NAME, WT_MAX_NAME_LEN);
    c->NAME[WT_MAX_NAME_LEN - 1] = 0;
    c->ACTIVE = TRUE;
    c->PKTDATA = BASE_PKTDATA | ((ev->axes & OTDWT_AX_WHEEL) ? PK_TANGENT_PRESSURE : 0);
    c->BUTTONS = 3;
    c->BUTTONBITS = 3;
    memcpy(c->BTNNAMES, btnnames, sizeof(btnnames));
    c->cchBTNNAMES = ARRAYSIZE(btnnames);
    for (i = 0; i < 3; i++)
    {
        c->BUTTONMAP[i] = i;
        c->SYSBTNMAP[i] = 1 << i;
    }
    c->NPBUTTON = 0;
    c->NPBTNMARKS[0] = 0;
    c->NPBTNMARKS[1] = 1;
    c->PHYSID = ev->serial;
    c->TYPE = ev->tool_id;
    c->CAPABILITIES = CRC_MULTIMODE | ((ev->flags & OTDWT_F_ERASER) ? CRC_INVERT : 0);

    TRACE("new cursor %u: %s (tool %#x serial %#x)\n", gNumCursors, name, ev->tool_id, ev->serial);
    gSysDevice.NCSRTYPES = ++gNumCursors;
    return gNumCursors - 1;
}

/***********************************************************************
 * events -> WTPACKET (after Wine's motion_event/proximity_event)
 */
static int figure_deg(int x, int y)
{
    double angle = atan2((double)y, (double)x) + M_PI_2;
    if (angle <= 0) angle += 2 * M_PI;
    return (int)(0.5 + angle * 1800.0 / M_PI);
}

static DWORD get_changed_state(const WTPACKET *pkt)
{
    DWORD change = 0;

    if (pkt->pkX != last_packet.pkX) change |= PK_X;
    if (pkt->pkY != last_packet.pkY) change |= PK_Y;
    if (pkt->pkSerialNumber != last_packet.pkSerialNumber) change |= PK_SERIAL_NUMBER;
    if (pkt->pkTime != last_packet.pkTime) change |= PK_TIME;
    if (pkt->pkNormalPressure != last_packet.pkNormalPressure) change |= PK_NORMAL_PRESSURE;
    if (pkt->pkTangentPressure != last_packet.pkTangentPressure) change |= PK_TANGENT_PRESSURE;
    if (pkt->pkCursor != last_packet.pkCursor) change |= PK_CURSOR;
    if (pkt->pkButtons != last_packet.pkButtons) change |= PK_BUTTONS;
    if (memcmp(&pkt->pkOrientation, &last_packet.pkOrientation, sizeof(ORIENTATION)))
        change |= PK_ORIENTATION;
    return change;
}

static void fill_packet(WTPACKET *pkt, const otdwt_event *ev, int curnum)
{
    BOOL eraser = (ev->flags & OTDWT_F_ERASER) != 0;
    int tilt = (int)sqrt((double)ev->tilt_x * ev->tilt_x + (double)ev->tilt_y * ev->tilt_y);

    memset(pkt, 0, sizeof(*pkt));
    pkt->pkStatus = eraser ? TPS_INVERT : 0;
    if (ev->type == OTDWT_EV_PROXIMITY_OUT) pkt->pkStatus |= TPS_PROXIMITY;
    pkt->pkTime = ev->time_ms;
    pkt->pkSerialNumber = serial_counter++;
    pkt->pkCursor = curnum;
    pkt->pkButtons = ev->buttons;
    pkt->pkX = ev->x;
    pkt->pkY = ev->y;
    pkt->pkNormalPressure = ev->pressure;
    pkt->pkTangentPressure = (ev->axes & OTDWT_AX_WHEEL) ? ev->wheel : 0;
    if (ev->axes & OTDWT_AX_TILT)
    {
        pkt->pkOrientation.orAzimuth = figure_deg(ev->tilt_x, ev->tilt_y);
        pkt->pkOrientation.orAltitude = (900 - min(tilt, 900)) * (eraser ? -1 : 1);
    }
    else
        pkt->pkOrientation.orAltitude = eraser ? -900 : 900;
    if (ev->axes & OTDWT_AX_ROTATION)
        pkt->pkOrientation.orTwist = ((ev->rotation % 3600) + 3600) % 3600;
    pkt->pkChanged = get_changed_state(pkt);
    last_packet = *pkt;
}

static void handle_event(const otdwt_event *ev)
{
    WTPACKET pkt;
    int curnum;
    BOOL was_in;

    AcquireSRWLockExclusive(&cursor_lock);
    curnum = find_cursor(ev);
    if (curnum < 0 && ev->type != OTDWT_EV_PROXIMITY_OUT)
        curnum = add_cursor(ev);
    if (curnum < 0)
    {
        ReleaseSRWLockExclusive(&cursor_lock);
        return;
    }
    was_in = gSysCursor[curnum].in_proximity;
    gSysCursor[curnum].in_proximity = ev->type != OTDWT_EV_PROXIMITY_OUT;
    ReleaseSRWLockExclusive(&cursor_lock);

    /* a packet without a prior proximity-in (e.g. we attached mid-stroke) */
    if (ev->type == OTDWT_EV_PACKET && !was_in)
    {
        fill_packet(&pkt, ev, curnum);
        TABLET_DeliverPacket(&pkt, our_device, MAKELPARAM(1, 1));
    }

    fill_packet(&pkt, ev, curnum);
    switch (ev->type)
    {
    case OTDWT_EV_PROXIMITY_IN:
        TABLET_DeliverPacket(&pkt, our_device, MAKELPARAM(1, 1));
        break;
    case OTDWT_EV_PROXIMITY_OUT:
        if (was_in) TABLET_DeliverPacket(&pkt, our_device, MAKELPARAM(0, 1));
        break;
    default:
        TABLET_DeliverPacket(&pkt, our_device, 0);
        break;
    }
}

/***********************************************************************
 * ring reader
 */
static DWORD WINAPI reader_thread_proc(void *arg)
{
    struct ring_handle ring = {0};
    uint64_t read_index = 0;
    otdwt_event ev;

    while (!stopping)
    {
        if (!ring.ring)
        {
            if (!ring_open(&ring))
            {
                Sleep(500);
                continue;
            }
            read_index = ring.ring->write_index;   /* skip stale history */
            TRACE("attached to ring (producer pid %u)\n", ring.ring->producer_pid);
        }

        while (ring_read(&ring, &read_index, &ev))
            handle_event(&ev);

        /* auto-reset signal wakes one reader; others fall back to polling */
        if (ring.signal)
            WaitForSingleObject(ring.signal, 4);
        else
            Sleep(4);
    }
    ring_close(&ring);
    return 0;
}

/***********************************************************************
 * backend interface
 */
BOOL backend_init(void)
{
    HMODULE self;

    /* Our threads run our code, so once they exist the DLL must stay
     * mapped: pin it rather than trying to join threads from DllMain. */
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       (LPCWSTR)backend_init, &self);

    setup_device();
    if (fake_source_enabled())
        CloseHandle(CreateThread(NULL, 0, fake_source_thread, (void *)&stopping, 0, NULL));
    reader_thread = CreateThread(NULL, 0, reader_thread_proc, NULL, 0, NULL);
    return reader_thread != NULL;
}

void backend_shutdown(void)
{
    /* Only reached for a DLL that never started its threads (it is pinned
     * otherwise); nothing to join. */
    stopping = 1;
}

static inline UINT copy_out(void *target, const void *src, UINT size)
{
    if (target) memcpy(target, src, size);
    return size;
}

static UINT context_info(const LOGCONTEXTW *ctx, UINT index, void *out)
{
    switch (index)
    {
    case 0:            return copy_out(out, ctx, sizeof(LOGCONTEXTW));
    case CTX_NAME:     return copy_out(out, ctx->lcName, (wcslen(ctx->lcName) + 1) * sizeof(WCHAR));
    case CTX_OPTIONS:  return copy_out(out, &ctx->lcOptions, sizeof(UINT));
    case CTX_STATUS:   return copy_out(out, &ctx->lcStatus, sizeof(UINT));
    case CTX_LOCKS:    return copy_out(out, &ctx->lcLocks, sizeof(UINT));
    case CTX_MSGBASE:  return copy_out(out, &ctx->lcMsgBase, sizeof(UINT));
    case CTX_DEVICE:   return copy_out(out, &ctx->lcDevice, sizeof(UINT));
    case CTX_PKTRATE:  return copy_out(out, &ctx->lcPktRate, sizeof(UINT));
    case CTX_PKTDATA:  return copy_out(out, &ctx->lcPktData, sizeof(WTPKT));
    case CTX_PKTMODE:  return copy_out(out, &ctx->lcPktMode, sizeof(WTPKT));
    case CTX_MOVEMASK: return copy_out(out, &ctx->lcMoveMask, sizeof(WTPKT));
    case CTX_BTNDNMASK:return copy_out(out, &ctx->lcBtnDnMask, sizeof(DWORD));
    case CTX_BTNUPMASK:return copy_out(out, &ctx->lcBtnUpMask, sizeof(DWORD));
    case CTX_INORGX:   return copy_out(out, &ctx->lcInOrgX, sizeof(LONG));
    case CTX_INORGY:   return copy_out(out, &ctx->lcInOrgY, sizeof(LONG));
    case CTX_INORGZ:   return copy_out(out, &ctx->lcInOrgZ, sizeof(LONG));
    case CTX_INEXTX:   return copy_out(out, &ctx->lcInExtX, sizeof(LONG));
    case CTX_INEXTY:   return copy_out(out, &ctx->lcInExtY, sizeof(LONG));
    case CTX_INEXTZ:   return copy_out(out, &ctx->lcInExtZ, sizeof(LONG));
    case CTX_OUTORGX:  return copy_out(out, &ctx->lcOutOrgX, sizeof(LONG));
    case CTX_OUTORGY:  return copy_out(out, &ctx->lcOutOrgY, sizeof(LONG));
    case CTX_OUTORGZ:  return copy_out(out, &ctx->lcOutOrgZ, sizeof(LONG));
    case CTX_OUTEXTX:  return copy_out(out, &ctx->lcOutExtX, sizeof(LONG));
    case CTX_OUTEXTY:  return copy_out(out, &ctx->lcOutExtY, sizeof(LONG));
    case CTX_OUTEXTZ:  return copy_out(out, &ctx->lcOutExtZ, sizeof(LONG));
    case CTX_SENSX:    return copy_out(out, &ctx->lcSensX, sizeof(LONG));
    case CTX_SENSY:    return copy_out(out, &ctx->lcSensY, sizeof(LONG));
    case CTX_SENSZ:    return copy_out(out, &ctx->lcSensZ, sizeof(LONG));
    case CTX_SYSMODE:  return copy_out(out, &ctx->lcSysMode, sizeof(LONG));
    case CTX_SYSORGX:  return copy_out(out, &ctx->lcSysOrgX, sizeof(LONG));
    case CTX_SYSORGY:  return copy_out(out, &ctx->lcSysOrgY, sizeof(LONG));
    case CTX_SYSEXTX:  return copy_out(out, &ctx->lcSysExtX, sizeof(LONG));
    case CTX_SYSEXTY:  return copy_out(out, &ctx->lcSysExtY, sizeof(LONG));
    case CTX_SYSSENSX: return copy_out(out, &ctx->lcSysSensX, sizeof(LONG));
    case CTX_SYSSENSY: return copy_out(out, &ctx->lcSysSensY, sizeof(LONG));
    }
    FIXME("context index %u unhandled\n", index);
    return 0;
}

static UINT cursor_info(const WTI_CURSORS_INFO *c, UINT index, void *out)
{
    switch (index)
    {
    case CSR_NAME:        return copy_out(out, c->NAME, (wcslen(c->NAME) + 1) * sizeof(WCHAR));
    case CSR_ACTIVE:      return copy_out(out, &c->ACTIVE, sizeof(BOOL));
    case CSR_PKTDATA:     return copy_out(out, &c->PKTDATA, sizeof(WTPKT));
    case CSR_BUTTONS:     return copy_out(out, &c->BUTTONS, sizeof(BYTE));
    case CSR_BUTTONBITS:  return copy_out(out, &c->BUTTONBITS, sizeof(BYTE));
    case CSR_BTNNAMES:    return copy_out(out, c->BTNNAMES, c->cchBTNNAMES * sizeof(WCHAR));
    case CSR_BUTTONMAP:   return copy_out(out, c->BUTTONMAP, sizeof(c->BUTTONMAP));
    case CSR_SYSBTNMAP:   return copy_out(out, c->SYSBTNMAP, sizeof(c->SYSBTNMAP));
    case CSR_NPBUTTON:    return copy_out(out, &c->NPBUTTON, sizeof(BYTE));
    case CSR_NPBTNMARKS:  return copy_out(out, c->NPBTNMARKS, sizeof(c->NPBTNMARKS));
    case CSR_TPBUTTON:    return copy_out(out, &c->TPBUTTON, sizeof(BYTE));
    case CSR_TPBTNMARKS:  return copy_out(out, c->TPBTNMARKS, sizeof(c->TPBTNMARKS));
    case CSR_PHYSID:      return copy_out(out, &c->PHYSID, sizeof(DWORD));
    case CSR_MODE:        return copy_out(out, &c->MODE, sizeof(UINT));
    case CSR_MINPKTDATA:  return copy_out(out, &c->MINPKTDATA, sizeof(UINT));
    case CSR_MINBUTTONS:  return copy_out(out, &c->MINBUTTONS, sizeof(UINT));
    case CSR_CAPABILITIES:return copy_out(out, &c->CAPABILITIES, sizeof(UINT));
    case CSR_TYPE:        return copy_out(out, &c->TYPE, sizeof(UINT));
    case CSR_NPRESPONSE:
    case CSR_TPRESPONSE:  return 0;   /* linear response; not reported, as in Wine */
    }
    FIXME("cursor index %u unhandled\n", index);
    return 0;
}

static UINT device_info(UINT index, void *out)
{
    const WTI_DEVICES_INFO *d = &gSysDevice;

    switch (index)
    {
    case DVC_NAME:        return copy_out(out, d->NAME, (wcslen(d->NAME) + 1) * sizeof(WCHAR));
    case DVC_HARDWARE:    return copy_out(out, &d->HARDWARE, sizeof(UINT));
    case DVC_NCSRTYPES:   return copy_out(out, &d->NCSRTYPES, sizeof(UINT));
    case DVC_FIRSTCSR:    return copy_out(out, &d->FIRSTCSR, sizeof(UINT));
    case DVC_PKTRATE:     return copy_out(out, &d->PKTRATE, sizeof(UINT));
    case DVC_PKTDATA:     return copy_out(out, &d->PKTDATA, sizeof(WTPKT));
    case DVC_PKTMODE:     return copy_out(out, &d->PKTMODE, sizeof(WTPKT));
    case DVC_CSRDATA:     return copy_out(out, &d->CSRDATA, sizeof(WTPKT));
    case DVC_XMARGIN:     return copy_out(out, &d->XMARGIN, sizeof(INT));
    case DVC_YMARGIN:     return copy_out(out, &d->YMARGIN, sizeof(INT));
    case DVC_X:           return copy_out(out, &d->X, sizeof(AXIS));
    case DVC_Y:           return copy_out(out, &d->Y, sizeof(AXIS));
    case DVC_NPRESSURE:   return copy_out(out, &d->NPRESSURE, sizeof(AXIS));
    case DVC_TPRESSURE:   return copy_out(out, &d->TPRESSURE, sizeof(AXIS));
    case DVC_ORIENTATION: return copy_out(out, d->ORIENTATION, sizeof(d->ORIENTATION));
    case DVC_PNPID:       return copy_out(out, d->PNPID, (wcslen(d->PNPID) + 1) * sizeof(WCHAR));
    case DVC_ZMARGIN:
    case DVC_Z:
    case DVC_ROTATION:    return 0;   /* unsupported, as in Wine */
    }
    FIXME("device index %u unhandled\n", index);
    return 0;
}

UINT backend_info(UINT category, UINT index, void *out)
{
    UINT rc = 0, num;
    WORD version;

    TRACE("(%u, %u, %p)\n", category, index, out);

    switch (category)
    {
    case 0:
        /* Largest buffer any query can need. Must be non-zero even before
         * any tool has been seen, or GTK considers Wintab absent. */
        return max(sizeof(LOGCONTEXTW), WT_MAX_NAME_LEN * sizeof(WCHAR));

    case WTI_INTERFACE:
        switch (index)
        {
        case IFC_WINTABID:
            return copy_out(out, L"otd-wintab (OpenTabletDriver)", sizeof(L"otd-wintab (OpenTabletDriver)"));
        case IFC_SPECVERSION:
            version = 0x0101;
            return copy_out(out, &version, sizeof(WORD));
        case IFC_IMPLVERSION:
            version = 0x0100;
            return copy_out(out, &version, sizeof(WORD));
        case IFC_NDEVICES:
            num = 1;
            return copy_out(out, &num, sizeof(num));
        case IFC_NCURSORS:
            AcquireSRWLockShared(&cursor_lock);
            num = gNumCursors;
            ReleaseSRWLockShared(&cursor_lock);
            return copy_out(out, &num, sizeof(num));
        case IFC_NCONTEXTS:
            num = 32;
            return copy_out(out, &num, sizeof(num));
        case IFC_CTXOPTIONS:
            num = CXO_SYSTEM | CXO_PEN | CXO_MESSAGES | CXO_CSRMESSAGES;
            return copy_out(out, &num, sizeof(num));
        case IFC_CTXSAVESIZE:
        case IFC_NEXTENSIONS:
        case IFC_NMANAGERS:
            num = 0;
            return copy_out(out, &num, sizeof(num));
        }
        FIXME("WTI_INTERFACE index %u unhandled\n", index);
        return 0;

    case WTI_DEFCONTEXT:
    case WTI_DEFSYSCTX:
        return context_info(&gSysContext, index, out);
    }

    if (category == WTI_DDCTXS + our_device || category == WTI_DSCTXS + our_device)
        return context_info(&gSysContext, index, out);

    if (category == WTI_DEVICES + our_device)
    {
        AcquireSRWLockShared(&cursor_lock);
        rc = device_info(index, out);
        ReleaseSRWLockShared(&cursor_lock);
        return rc;
    }

    if (category >= WTI_CURSORS && category < WTI_CURSORS + CURSORMAX)
    {
        /* Apps poll cursor slots to see what exists; empty slots return 0 */
        AcquireSRWLockShared(&cursor_lock);
        if (category - WTI_CURSORS < gNumCursors)
            rc = cursor_info(&gSysCursor[category - WTI_CURSORS], index, out);
        ReleaseSRWLockShared(&cursor_lock);
        return rc;
    }

    TRACE("category %u not available\n", category);
    return 0;
}
