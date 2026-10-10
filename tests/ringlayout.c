/*
 * otd-wintab: prints the C layout of the shared ring, so the C# mirror
 * (src/otd-plugin/Core/RingProtocol.cs) can be checked against it.
 *
 * Copyright 2026 otd-wintab contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stddef.h>
#include <stdio.h>

#include "otdwt_ring.h"

#define F(type, field) printf("%s.%s=%u\n", #type, #field, (unsigned)offsetof(type, field))

int main(void)
{
    printf("otdwt_event.size=%u\n", (unsigned)sizeof(otdwt_event));
    F(otdwt_event, type);
    F(otdwt_event, flags);
    F(otdwt_event, axes);
    F(otdwt_event, tool_id);
    F(otdwt_event, serial);
    F(otdwt_event, buttons);
    F(otdwt_event, x);
    F(otdwt_event, y);
    F(otdwt_event, pressure);
    F(otdwt_event, tilt_x);
    F(otdwt_event, tilt_y);
    F(otdwt_event, wheel);
    F(otdwt_event, rotation);
    F(otdwt_event, time_ms);
    F(otdwt_event, name);
    printf("otdwt_ring.size=%u\n", (unsigned)sizeof(otdwt_ring));
    F(otdwt_ring, magic);
    F(otdwt_ring, version);
    F(otdwt_ring, ring_size);
    F(otdwt_ring, event_size);
    F(otdwt_ring, producer_pid);
    F(otdwt_ring, write_index);
    F(otdwt_ring, ring);
    printf("const.OTDWT_MAGIC=%u\n", OTDWT_MAGIC);
    printf("const.OTDWT_VERSION=%u\n", OTDWT_VERSION);
    printf("const.OTDWT_RING_SIZE=%u\n", OTDWT_RING_SIZE);
    printf("const.OTDWT_NAME_LEN=%u\n", OTDWT_NAME_LEN);
    printf("const.OTDWT_POS_SCALE=%u\n", OTDWT_POS_SCALE);
    printf("const.OTDWT_PRESSURE_MAX=%u\n", OTDWT_PRESSURE_MAX);
    printf("const.OTDWT_WHEEL_MAX=%u\n", OTDWT_WHEEL_MAX);
    return 0;
}
