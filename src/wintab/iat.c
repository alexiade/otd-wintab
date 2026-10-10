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

#include <string.h>

#include "wine_compat.h"
#include "iat.h"

int iat_patch_module(HMODULE module, const char *import_dll, const char *function, void *replacement)
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

        if (_stricmp((const char *)(base + imp->Name), import_dll)) continue;
        if (!imp->OriginalFirstThunk) continue;
        names = (IMAGE_THUNK_DATA *)(base + imp->OriginalFirstThunk);
        iat = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++)
        {
            IMAGE_IMPORT_BY_NAME *ibn;
            DWORD old;

            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            ibn = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((const char *)ibn->Name, function)) continue;
            if ((void *)iat->u1.Function == replacement) continue;
            if (VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), PAGE_READWRITE, &old))
            {
                iat->u1.Function = (ULONG_PTR)replacement;
                VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), old, &old);
                patched++;
            }
        }
    }
    return patched;
}
