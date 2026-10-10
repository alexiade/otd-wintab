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

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "wine_compat.h"
#include "ring.h"

/* OTD_WINTAB_INSTANCE=<suffix> selects a private ring; the test harness
 * uses it so tests never mix with a live producer. */
static void ring_names(WCHAR *mapping, WCHAR *signal, size_t len)
{
    WCHAR inst[64];
    DWORD n = GetEnvironmentVariableW(L"OTD_WINTAB_INSTANCE", inst, ARRAYSIZE(inst));

    if (n > 0 && n < ARRAYSIZE(inst))
    {
        _snwprintf(mapping, len, L"%ls-%ls", OTDWT_MAPPING_NAME, inst);
        _snwprintf(signal, len, L"%ls-%ls", OTDWT_EVENT_NAME, inst);
    }
    else
    {
        _snwprintf(mapping, len, L"%ls", OTDWT_MAPPING_NAME);
        _snwprintf(signal, len, L"%ls", OTDWT_EVENT_NAME);
    }
    mapping[len - 1] = signal[len - 1] = 0;
}

static void init_header(otdwt_ring *ring)
{
    ring->magic = OTDWT_MAGIC;
    ring->version = OTDWT_VERSION;
    ring->ring_size = OTDWT_RING_SIZE;
    ring->event_size = sizeof(otdwt_event);
    ring->producer_pid = GetCurrentProcessId();
}

BOOL ring_create(struct ring_handle *h)
{
    WCHAR mapping[128], signal[128];

    ring_names(mapping, signal, ARRAYSIZE(mapping));
    memset(h, 0, sizeof(*h));
    h->mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                    sizeof(otdwt_ring), mapping);
    if (!h->mapping) return FALSE;
    h->ring = MapViewOfFile(h->mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(otdwt_ring));
    h->signal = CreateEventW(NULL, FALSE, FALSE, signal);
    if (!h->ring || !h->signal)
    {
        ring_close(h);
        return FALSE;
    }
    init_header(h->ring);
    return TRUE;
}

BOOL ring_open(struct ring_handle *h)
{
    WCHAR mapping[128], signal[128];
    otdwt_ring *r;

    ring_names(mapping, signal, ARRAYSIZE(mapping));
    memset(h, 0, sizeof(*h));
    h->mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, mapping);
    if (!h->mapping) return FALSE;
    r = MapViewOfFile(h->mapping, FILE_MAP_READ, 0, 0, sizeof(otdwt_ring));
    h->ring = r;
    if (!r || r->magic != OTDWT_MAGIC || r->version != OTDWT_VERSION ||
        r->ring_size != OTDWT_RING_SIZE || r->event_size != sizeof(otdwt_event))
    {
        if (r) ERR("ring layout mismatch (magic %08x version %u)\n", r->magic, r->version);
        ring_close(h);
        return FALSE;
    }
    h->signal = OpenEventW(SYNCHRONIZE, FALSE, signal);
    return TRUE;
}

void ring_close(struct ring_handle *h)
{
    if (h->ring) UnmapViewOfFile((void *)h->ring);
    if (h->mapping) CloseHandle(h->mapping);
    if (h->signal) CloseHandle(h->signal);
    memset(h, 0, sizeof(*h));
}

void ring_publish(struct ring_handle *h, const otdwt_event *ev)
{
    otdwt_ring *r = h->ring;
    uint64_t idx = r->write_index;

    r->ring[idx & (OTDWT_RING_SIZE - 1)] = *ev;
    __atomic_store_n(&r->write_index, idx + 1, __ATOMIC_RELEASE);
    SetEvent(h->signal);
}

int ring_read(struct ring_handle *h, uint64_t *read_index, otdwt_event *ev)
{
    const otdwt_ring *r = h->ring;
    uint64_t w = __atomic_load_n(&r->write_index, __ATOMIC_ACQUIRE);

    if (w < *read_index)            /* producer restarted */
        *read_index = w;
    if (w == *read_index)
        return 0;
    if (w - *read_index > OTDWT_RING_SIZE)
    {
        WARN("ring overrun, skipping %llu events\n",
             (unsigned long long)(w - *read_index - OTDWT_RING_SIZE));
        *read_index = w - OTDWT_RING_SIZE;
    }

    *ev = r->ring[*read_index & (OTDWT_RING_SIZE - 1)];

    /* if the producer lapped us while copying, the slot may be torn */
    w = __atomic_load_n(&r->write_index, __ATOMIC_ACQUIRE);
    if (w - *read_index > OTDWT_RING_SIZE)
        return ring_read(h, read_index, ev);

    (*read_index)++;
    return 1;
}
