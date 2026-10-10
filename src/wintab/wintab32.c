/*
 * WinTab32 library
 *
 * Copyright 2003 CodeWeavers, Aric Stewart
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * otd-wintab: adapted from Wine dlls/wintab32/wintab32.c. Wine routes
 * packets from its X11 driver through an internal message window; here the
 * backend thread delivers them directly (TABLET_DeliverPacket), so only the
 * DllMain bookkeeping remains.
 */

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#define NOFIX32
#include "wintab.h"
#include "wintab_internal.h"
#include "backend.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wintab32);

void otdwt_log_init(void);
void otdwt_log_shutdown(void);

CRITICAL_SECTION csTablet;

BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID lpReserved)
{
    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hInstDLL);
            InitializeCriticalSection(&csTablet);
            otdwt_log_init();
            TRACE("Initialization\n");
            break;
        case DLL_PROCESS_DETACH:
            if (lpReserved) break;  /* process exit: don't touch other threads */
            TRACE("Detaching\n");
            backend_shutdown();
            DeleteCriticalSection(&csTablet);
            otdwt_log_shutdown();
            break;
    }
    return TRUE;
}
