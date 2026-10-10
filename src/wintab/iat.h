/*
 * otd-wintab: import address table patching.
 *
 * Copyright 2026 otd-wintab contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef OTDWT_IAT_H
#define OTDWT_IAT_H

#include <windows.h>

/* Points `module`'s import of import_dll!function at `replacement`.
 * Returns the number of entries changed. */
int iat_patch_module(HMODULE module, const char *import_dll, const char *function, void *replacement);

#endif
