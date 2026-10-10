/*
 * otd-wintab: interface between the Wintab API layer (context.c, adapted
 * from Wine) and the tablet backend. It stands in for the calls Wine's
 * wintab32 makes into its USER driver (NtUserWintabInit/Info/Packet).
 *
 * Copyright 2026 otd-wintab contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef OTDWT_BACKEND_H
#define OTDWT_BACKEND_H

#include <windows.h>

/* Called once, lazily, on the first Wintab call. FALSE disables Wintab. */
BOOL backend_init(void);

/* Answers WTInfoW(category, index, output). Strings are returned as WCHAR;
 * output may be NULL to query the size. */
UINT backend_info(UINT category, UINT index, void *output);

/* Called from DllMain on process detach. */
void backend_shutdown(void);

#endif /* OTDWT_BACKEND_H */
