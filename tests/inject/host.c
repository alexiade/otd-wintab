/*
 * otd-wintab test: stands in for gimp.exe. Run from a directory holding
 * the proxy dwmapi.dll, otd-wintab32.dll and the mock libgdk-3-0.dll.
 *
 *   inject_host.exe           expect redirection to otd-wintab32.dll
 *   inject_host.exe disabled  expect OTD_WINTAB_DISABLE to switch it off
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
#include <stdio.h>
#include <string.h>
#include <wchar.h>

HMODULE mock_gdk_load_wintab(void);
HMODULE mock_gdk_load_other(void);
BOOL mock_gdk_composition(void);

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("    FAIL %s:%d: ", __FILE__, __LINE__); \
                                              printf(__VA_ARGS__); printf("\n"); } } while (0)

static BOOL module_is(HMODULE m, const WCHAR *name)
{
    WCHAR path[MAX_PATH];
    const WCHAR *base;
    if (!m || !GetModuleFileNameW(m, path, MAX_PATH)) return FALSE;
    base = wcsrchr(path, L'\\');
    return !_wcsicmp(base ? base + 1 : path, name);
}

int main(int argc, char **argv)
{
    BOOL disabled = argc > 1 && !strcmp(argv[1], "disabled");
    HMODULE proxy = GetModuleHandleW(L"dwmapi.dll");
    HMODULE real = LoadLibraryExW(L"C:\\Windows\\System32\\dwmapi.dll", NULL, 0);
    HMODULE wintab, other;
    WCHAR proxy_path[MAX_PATH] = {0};

    setvbuf(stdout, NULL, _IONBF, 0);

    /* the proxy, not the system DLL, was loaded for our import */
    GetModuleFileNameW(proxy, proxy_path, MAX_PATH);
    CHECK(GetProcAddress(proxy, "otdwt_proxy_marker") != NULL, "dwmapi.dll is not the proxy (%ls)", proxy_path);

    /* forwarding: named and ordinal-only exports resolve into the real DLL */
    CHECK(mock_gdk_composition(), "DwmIsCompositionEnabled through the proxy failed");
    CHECK(GetProcAddress(proxy, "DwmFlush") == GetProcAddress(real, "DwmFlush"), "named forwarder");
    CHECK(GetProcAddress(proxy, MAKEINTRESOURCEA(106)) == GetProcAddress(real, MAKEINTRESOURCEA(106)),
          "ordinal-only forwarder");

    wintab = mock_gdk_load_wintab();
    other = mock_gdk_load_other();
    CHECK(module_is(other, L"version.dll"), "unrelated LoadLibrary calls pass through");
    if (disabled)
        CHECK(!module_is(wintab, L"otd-wintab32.dll"), "redirected although OTD_WINTAB_DISABLE is set");
    else
    {
        CHECK(module_is(wintab, L"otd-wintab32.dll"), "Wintab32.dll request not redirected");
        CHECK(wintab && GetProcAddress(wintab, "WTInfoA"), "provider exports WTInfoA");
    }

    printf("%s\n", failures ? "inject: FAILED" : "inject: ok");
    return failures ? 1 : 0;
}
