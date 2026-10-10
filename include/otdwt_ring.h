/*
 * otd-wintab: shared-memory event ring between the tablet producer
 * (the OpenTabletDriver output plugin, or the built-in fake source)
 * and the Wintab provider DLL loaded in applications.
 *
 * Copyright 2026 otd-wintab contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Layout rules: everything is little-endian, naturally aligned, fixed size.
 * The C# side mirrors these structs exactly; bump OTDWT_VERSION on any change.
 *
 * Concurrency: single producer, any number of readers. The producer fills
 * ring[write_index % OTDWT_RING_SIZE] and then publishes it by incrementing
 * write_index (release). A reader copies the slot and re-checks write_index
 * afterwards; if the producer lapped it, the copy is discarded.
 */

#ifndef OTDWT_RING_H
#define OTDWT_RING_H

#include <stdint.h>

/* If the environment variable OTD_WINTAB_INSTANCE is set, both names get
 * "-<value>" appended, giving a private ring (used by the test harness). */
#define OTDWT_MAPPING_NAME  L"Local\\otd-wintab-ring"
#define OTDWT_EVENT_NAME    L"Local\\otd-wintab-signal"

#define OTDWT_MAGIC         0x5457544Fu /* "OTWT" */
#define OTDWT_VERSION       1u
#define OTDWT_RING_SIZE     1024u       /* power of two */
#define OTDWT_NAME_LEN      64u         /* UTF-8, NUL terminated */

/* Positions are in virtual-screen pixels times this factor, relative to the
 * virtual screen's top-left corner, Y growing downward. */
#define OTDWT_POS_SCALE     8

/* Normalized axis ranges */
#define OTDWT_PRESSURE_MAX  32767
#define OTDWT_WHEEL_MAX     1023

enum otdwt_event_type
{
    OTDWT_EV_PROXIMITY_IN  = 1,   /* tool entered; name/tool fields valid */
    OTDWT_EV_PROXIMITY_OUT = 2,   /* tool left */
    OTDWT_EV_PACKET        = 3,   /* tool moved / changed state */
};

enum otdwt_event_flags
{
    OTDWT_F_ERASER         = 1u << 0,  /* this tool is an eraser end */
};

enum otdwt_axes
{
    OTDWT_AX_PRESSURE      = 1u << 0,
    OTDWT_AX_TILT          = 1u << 1,
    OTDWT_AX_WHEEL         = 1u << 2,  /* airbrush finger wheel -> tangential pressure */
    OTDWT_AX_ROTATION      = 1u << 3,  /* barrel rotation (Art Pen / marker) -> twist */
};

typedef struct otdwt_event
{
    uint32_t type;          /* enum otdwt_event_type */
    uint32_t flags;         /* enum otdwt_event_flags */
    uint32_t axes;          /* enum otdwt_axes this tool supports */
    uint32_t tool_id;       /* vendor tool type id (Wacom: e.g. 0x823 grip pen) */
    uint32_t serial;        /* physical tool serial, 0 if the tablet has none */
    uint32_t buttons;       /* bit0 tip, bit1 lower barrel, bit2 upper barrel */
    int32_t  x, y;          /* see OTDWT_POS_SCALE */
    uint32_t pressure;      /* 0..OTDWT_PRESSURE_MAX */
    int32_t  tilt_x;        /* tenths of a degree, + = toward right */
    int32_t  tilt_y;        /* tenths of a degree, + = toward user */
    uint32_t wheel;         /* 0..OTDWT_WHEEL_MAX */
    int32_t  rotation;      /* tenths of a degree, 0..3599 */
    uint32_t time_ms;       /* producer GetTickCount() */
    char     name[OTDWT_NAME_LEN]; /* tool display name, e.g. "Airbrush" */
} otdwt_event;

typedef struct otdwt_ring
{
    uint32_t magic;
    uint32_t version;
    uint32_t ring_size;
    uint32_t event_size;
    uint32_t producer_pid;
    uint32_t reserved0;
    volatile uint64_t write_index;   /* total events ever published */
    uint64_t reserved[4];
    otdwt_event ring[OTDWT_RING_SIZE];
} otdwt_ring;

#endif /* OTDWT_RING_H */
