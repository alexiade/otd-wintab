/*
 * otd-wintab: helpers for the shared event ring (see include/otdwt_ring.h).
 *
 * Copyright 2026 otd-wintab contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef OTDWT_RING_HELPERS_H
#define OTDWT_RING_HELPERS_H

#include <windows.h>
#include "otdwt_ring.h"

struct ring_handle
{
    HANDLE mapping;
    HANDLE signal;      /* auto-reset; set after each publish */
    otdwt_ring *ring;
};

/* producer side */
BOOL ring_create(struct ring_handle *h);
void ring_publish(struct ring_handle *h, const otdwt_event *ev);

/* consumer side */
BOOL ring_open(struct ring_handle *h);
/* Returns 1 and advances *read_index if an event was read, 0 if none. */
int ring_read(struct ring_handle *h, uint64_t *read_index, otdwt_event *ev);

void ring_close(struct ring_handle *h);

#endif
