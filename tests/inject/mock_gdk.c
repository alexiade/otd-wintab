/*
 * otd-wintab test: stands in for GTK's libgdk-3-0.dll. Imports dwmapi.dll
 * like the real one and loads Wintab exactly like gdkdevicemanager-win32.c
 * (system directory + "\\Wintab32.dll" through LoadLibraryA).
 *
 * Copyright 2026 otd-wintab contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <string.h>

__declspec(dllexport) HMODULE mock_gdk_load_wintab(void)
{
    char path[MAX_PATH];
    UINT n = GetSystemDirectoryA(path, MAX_PATH);
    if (n == 0 || n + 14 >= MAX_PATH) return NULL;
    if (path[n - 1] != '\\') strcat(path, "\\");
    strcat(path, "Wintab32.dll");
    return LoadLibraryA(path);
}

__declspec(dllexport) HMODULE mock_gdk_load_other(void)
{
    return LoadLibraryA("version.dll");
}

/* a real import of dwmapi, so the proxy is a dependency of this module */
__declspec(dllexport) BOOL mock_gdk_composition(void)
{
    BOOL on = FALSE;
    return SUCCEEDED(DwmIsCompositionEnabled(&on));
}
