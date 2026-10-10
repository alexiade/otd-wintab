/*
 * otd-wintab: prints the events published on the shared ring, live.
 * A diagnostic for the OpenTabletDriver side: run it, use the pen.
 *
 *   ringwatch.exe [seconds]      (default: until Ctrl+C)
 *
 * Copyright 2026 otd-wintab contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>

#include "wine_compat.h"
#include "ring.h"

static const char *type_name(uint32_t t)
{
    switch (t)
    {
    case OTDWT_EV_PROXIMITY_IN:  return "IN ";
    case OTDWT_EV_PROXIMITY_OUT: return "OUT";
    case OTDWT_EV_PACKET:        return "pkt";
    }
    return "???";
}

int main(int argc, char **argv)
{
    struct ring_handle ring;
    uint64_t idx;
    otdwt_event ev, last = {0};
    DWORD until = argc > 1 ? GetTickCount() + 1000u * (DWORD)atoi(argv[1]) : 0;
    unsigned skipped = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    while (!ring_open(&ring))
    {
        if (until && GetTickCount() > until) { printf("no ring (is the plugin's output mode active?)\n"); return 1; }
        Sleep(200);
    }
    idx = ring.ring->write_index;
    printf("attached (producer pid %u, %llu events so far)\n", ring.ring->producer_pid,
           (unsigned long long)idx);

    while (!until || GetTickCount() < until)
    {
        while (ring_read(&ring, &idx, &ev))
        {
            /* print every proximity event; packets only when something
             * other than the position changed, to keep the output readable */
            if (ev.type == OTDWT_EV_PACKET && last.type == OTDWT_EV_PACKET &&
                ev.buttons == last.buttons && ev.pressure / 1024 == last.pressure / 1024 &&
                ev.wheel / 64 == last.wheel / 64 && ev.rotation / 150 == last.rotation / 150 &&
                ev.serial == last.serial)
            {
                skipped++;
                last = ev;
                continue;
            }
            printf("%s %-24.24s tool %#07x serial %#010x %s| x %7.1f y %7.1f p %5u btn %u "
                   "tilt %5.1f %5.1f wheel %4u rot %5.1f axes %#x  (+%u)\n",
                   type_name(ev.type), ev.name, ev.tool_id, ev.serial,
                   (ev.flags & OTDWT_F_ERASER) ? "E " : "  ",
                   ev.x / (double)OTDWT_POS_SCALE, ev.y / (double)OTDWT_POS_SCALE, ev.pressure,
                   ev.buttons, ev.tilt_x / 10.0, ev.tilt_y / 10.0, ev.wheel, ev.rotation / 10.0,
                   ev.axes, skipped);
            skipped = 0;
            last = ev;
        }
        if (ring.signal) WaitForSingleObject(ring.signal, 20);
        else Sleep(20);
    }
    ring_close(&ring);
    return 0;
}
