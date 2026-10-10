/*
 * otd-wintab: per-application injection of the Wintab provider.
 *
 * GTK 3 loads Wintab with LoadLibraryA("<system dir>\\Wintab32.dll"), so the
 * provider can't simply be placed next to the application. Instead, this
 * DLL is installed as dwmapi.dll in the application's directory (GTK and
 * GIMP import dwmapi.dll, and it is not a KnownDLL, so the copy next to the
 * executable wins). It forwards every dwmapi export to the real system DLL
 * (see the generated .def), and patches the LoadLibrary imports of the GTK
 * module so that a request for Wintab32.dll yields otd-wintab32.dll from
 * this directory. Everything else is passed through untouched.
 *
 * Set OTD_WINTAB_DISABLE=1 to turn the redirection off without uninstalling.
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
#include <string.h>
#include <wchar.h>

/* Modules whose Wintab loading we redirect */
static const char *const target_modules[] = { "libgdk-3-0.dll" };

static WCHAR provider_path[MAX_PATH];

/* Marker so the install scripts can recognise our proxy */
__declspec(dllexport) const char otdwt_proxy_marker[] = "otd-wintab dwmapi proxy";

static BOOL is_wintab_request_w(const WCHAR *path)
{
    const WCHAR *base;
    if (!path) return FALSE;
    base = wcsrchr(path, L'\\');
    base = base ? base + 1 : path;
    return !_wcsicmp(base, L"wintab32.dll") || !_wcsicmp(base, L"wintab32");
}

static BOOL is_wintab_request_a(const char *path)
{
    const char *base;
    if (!path) return FALSE;
    base = strrchr(path, '\\');
    base = base ? base + 1 : path;
    return !_stricmp(base, "wintab32.dll") || !_stricmp(base, "wintab32");
}

static HMODULE WINAPI hook_LoadLibraryA(LPCSTR name)
{
    if (is_wintab_request_a(name)) return LoadLibraryW(provider_path);
    return LoadLibraryA(name);
}

static HMODULE WINAPI hook_LoadLibraryW(LPCWSTR name)
{
    if (is_wintab_request_w(name)) return LoadLibraryW(provider_path);
    return LoadLibraryW(name);
}

static HMODULE WINAPI hook_LoadLibraryExA(LPCSTR name, HANDLE file, DWORD flags)
{
    if (is_wintab_request_a(name)) return LoadLibraryExW(provider_path, file, flags);
    return LoadLibraryExA(name, file, flags);
}

static HMODULE WINAPI hook_LoadLibraryExW(LPCWSTR name, HANDLE file, DWORD flags)
{
    if (is_wintab_request_w(name)) return LoadLibraryExW(provider_path, file, flags);
    return LoadLibraryExW(name, file, flags);
}

static const struct { const char *name; void *hook; } hooks[] =
{
    { "LoadLibraryA",   (void *)hook_LoadLibraryA },
    { "LoadLibraryW",   (void *)hook_LoadLibraryW },
    { "LoadLibraryExA", (void *)hook_LoadLibraryExA },
    { "LoadLibraryExW", (void *)hook_LoadLibraryExW },
};

/* Rewrites the import address table entries of `module` for the
 * LoadLibrary family. Returns the number of entries patched. */
static int patch_imports(HMODULE module)
{
    BYTE *base = (BYTE *)module;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *imp;
    int patched = 0;

    if (!dir->VirtualAddress) return 0;
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress); imp->Name; imp++)
    {
        IMAGE_THUNK_DATA *names, *iat;

        if (!imp->OriginalFirstThunk) continue;     /* no name table to match against */
        names = (IMAGE_THUNK_DATA *)(base + imp->OriginalFirstThunk);
        iat = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++)
        {
            IMAGE_IMPORT_BY_NAME *ibn;
            size_t i;

            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            ibn = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            for (i = 0; i < ARRAYSIZE(hooks); i++)
            {
                DWORD old;
                if (strcmp((const char *)ibn->Name, hooks[i].name)) continue;
                if ((void *)iat->u1.Function == hooks[i].hook) break;
                if (VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), PAGE_READWRITE, &old))
                {
                    iat->u1.Function = (ULONG_PTR)hooks[i].hook;
                    VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), old, &old);
                    patched++;
                }
                break;
            }
        }
    }
    return patched;
}

static void patch_targets(void)
{
    size_t i;
    for (i = 0; i < ARRAYSIZE(target_modules); i++)
    {
        HMODULE m = GetModuleHandleA(target_modules[i]);
        if (m) patch_imports(m);
    }
}

/* Runs once the loader lock is released, in case the target's imports
 * were not yet bound when DllMain ran. */
static DWORD WINAPI repatch_thread(void *arg)
{
    patch_targets();
    return 0;
}

static BOOL redirection_disabled(void)
{
    char v[8];
    DWORD n = GetEnvironmentVariableA("OTD_WINTAB_DISABLE", v, sizeof(v));
    return n > 0 && n < sizeof(v) && v[0] != '0';
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        WCHAR *slash;
        HANDLE t;

        DisableThreadLibraryCalls(inst);
        if (redirection_disabled()) return TRUE;

        GetModuleFileNameW(inst, provider_path, MAX_PATH);
        slash = wcsrchr(provider_path, L'\\');
        if (!slash || (size_t)(slash - provider_path) + 20 >= MAX_PATH) return TRUE;
        wcscpy(slash + 1, L"otd-wintab32.dll");
        if (GetFileAttributesW(provider_path) == INVALID_FILE_ATTRIBUTES) return TRUE;

        patch_targets();
        t = CreateThread(NULL, 0, repatch_thread, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
