/*
 * otd-wintab: a fake tablet producer for testing the provider without
 * OpenTabletDriver. Enabled with OTD_WINTAB_FAKE=1. It follows the mouse:
 *
 *   Scroll Lock     on: the pen is on the tablet (in proximity) and mouse
 *                   movement is pen movement; off: the pen is lifted and
 *                   the mouse is just the mouse
 *   left button     tip pressed, pressure pulses so dynamics are visible
 *   Caps Lock       on: "Airbrush" (with wheel), off: "Grip Pen" (with tilt)
 *   Num Lock        off: "Art Pen" (with rotation), overrides Caps Lock
 *   Shift           eraser end of the current tool
 *
 * Each tool has a fixed fake serial, so switching tools exercises the
 * dynamic cursor creation exactly like real styli would.
 *
 * Copyright 2026 otd-wintab contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <math.h>
#include <string.h>

#include "wine_compat.h"
#include "ring.h"

BOOL fake_source_enabled(void)
{
    char v[8];
    DWORD n = GetEnvironmentVariableA("OTD_WINTAB_FAKE", v, sizeof(v));
    return n > 0 && n < sizeof(v) && v[0] != '0';
}

struct fake_tool
{
    const char *name, *eraser_name;
    uint32_t tool_id, serial, axes;
};

/* Tool ids follow Wacom's Intuos3 numbering, for realism */
static const struct fake_tool tools[] =
{
    { "Grip Pen", "Grip Pen Eraser", 0x823, 0x0FA4E001,
      OTDWT_AX_PRESSURE | OTDWT_AX_TILT },
    { "Airbrush", "Airbrush Eraser", 0x913, 0x0FA4E002,
      OTDWT_AX_PRESSURE | OTDWT_AX_TILT | OTDWT_AX_WHEEL },
    { "Art Pen",  "Art Pen Eraser",  0x885, 0x0FA4E003,
      OTDWT_AX_PRESSURE | OTDWT_AX_TILT | OTDWT_AX_ROTATION },
};

static void make_event(otdwt_event *ev, uint32_t type, const struct fake_tool *t, BOOL eraser)
{
    POINT p;
    double now = GetTickCount() / 1000.0;
    BOOL down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    memset(ev, 0, sizeof(*ev));
    GetCursorPos(&p);
    ev->type = type;
    ev->flags = eraser ? OTDWT_F_ERASER : 0;
    ev->axes = t->axes;
    ev->tool_id = eraser ? (t->tool_id | 0x8) : t->tool_id;
    ev->serial = t->serial;
    ev->x = (p.x - GetSystemMetrics(SM_XVIRTUALSCREEN)) * OTDWT_POS_SCALE;
    ev->y = (p.y - GetSystemMetrics(SM_YVIRTUALSCREEN)) * OTDWT_POS_SCALE;
    ev->buttons = down ? 1 : 0;
    ev->pressure = down ? (uint32_t)(OTDWT_PRESSURE_MAX * (0.55 + 0.4 * sin(now * 3.0))) : 0;
    ev->tilt_x = (int32_t)(300 * sin(now));
    ev->tilt_y = (int32_t)(300 * cos(now));
    ev->wheel = (uint32_t)(OTDWT_WHEEL_MAX * (0.5 + 0.5 * sin(now * 0.7)));
    ev->rotation = (int32_t)fmod(now * 600.0, 3600.0);
    ev->time_ms = GetTickCount();
    strncpy(ev->name, eraser ? t->eraser_name : t->name, OTDWT_NAME_LEN - 1);
}

DWORD WINAPI fake_source_thread(void *arg)
{
    volatile LONG *stopping = arg;
    struct ring_handle ring;
    const struct fake_tool *cur = NULL;
    BOOL cur_eraser = FALSE;
    POINT last = { -1, -1 };
    otdwt_event ev;

    if (!ring_create(&ring))
    {
        ERR("fake source: cannot create ring (%lu)\n", GetLastError());
        return 1;
    }
    TRACE("fake source running\n");

    while (!*stopping)
    {
        BOOL on_tablet = (GetKeyState(VK_SCROLL) & 1) != 0;
        const struct fake_tool *t = !(GetKeyState(VK_NUMLOCK) & 1) ? &tools[2] :
                                    (GetKeyState(VK_CAPITAL) & 1) ? &tools[1] : &tools[0];
        BOOL eraser = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        POINT p;

        if (cur && (!on_tablet || t != cur || eraser != cur_eraser))
        {
            make_event(&ev, OTDWT_EV_PROXIMITY_OUT, cur, cur_eraser);
            ring_publish(&ring, &ev);
            cur = NULL;
        }
        if (!on_tablet)
        {
            Sleep(20);
            continue;
        }
        if (!cur)
        {
            cur = t;
            cur_eraser = eraser;
            make_event(&ev, OTDWT_EV_PROXIMITY_IN, cur, cur_eraser);
            ring_publish(&ring, &ev);
        }

        GetCursorPos(&p);
        if (p.x != last.x || p.y != last.y || (GetAsyncKeyState(VK_LBUTTON) & 0x8000))
        {
            make_event(&ev, OTDWT_EV_PACKET, cur, cur_eraser);
            ring_publish(&ring, &ev);
            last = p;
        }
        Sleep(5);
    }

    if (cur)
    {
        make_event(&ev, OTDWT_EV_PROXIMITY_OUT, cur, cur_eraser);
        ring_publish(&ring, &ev);
    }
    ring_close(&ring);
    return 0;
}
