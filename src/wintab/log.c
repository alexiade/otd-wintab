/*
 * otd-wintab: tiny logger behind Wine's TRACE/FIXME/WARN/ERR macros.
 * Set OTD_WINTAB_LOG=<file path> to enable.
 *
 * Copyright 2026 otd-wintab contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "wine_compat.h"

int otdwt_log_enabled;
static FILE *log_file;
static CRITICAL_SECTION log_lock;

void otdwt_log_init(void)
{
    char path[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("OTD_WINTAB_LOG", path, sizeof(path));

    InitializeCriticalSection(&log_lock);
    if (n == 0 || n >= sizeof(path)) return;
    log_file = fopen(path, "a");
    if (log_file) otdwt_log_enabled = 1;
}

void otdwt_log_shutdown(void)
{
    otdwt_log_enabled = 0;
    if (log_file) fclose(log_file);
    log_file = NULL;
    DeleteCriticalSection(&log_lock);
}

void otdwt_log(const char *level, const char *func, const char *fmt, ...)
{
    va_list ap;

    if (!log_file) return;
    EnterCriticalSection(&log_lock);
    fprintf(log_file, "%08lu %04lx %s:%s ", GetTickCount(), GetCurrentThreadId(), level, func);
    va_start(ap, fmt);
    vfprintf(log_file, fmt, ap);
    va_end(ap);
    if (fmt[0] && fmt[strlen(fmt) - 1] != '\n') fputc('\n', log_file);
    fflush(log_file);
    LeaveCriticalSection(&log_lock);
}

/* Rotating per-thread scratch buffers, like Wine's debug helpers */
static char *scratch(void)
{
    static __thread char bufs[8][512];
    static __thread unsigned next;
    return bufs[next++ & 7];
}

const char *wine_dbg_sprintf(const char *fmt, ...)
{
    char *buf = scratch();
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, 512, fmt, ap);
    va_end(ap);
    return buf;
}

const char *wine_dbgstr_w(const WCHAR *s)
{
    char *buf = scratch();
    if (!s) return "(null)";
    buf[0] = '"';
    WideCharToMultiByte(CP_UTF8, 0, s, -1, buf + 1, 509, NULL, NULL);
    buf[510] = 0;
    strcat(buf, "\"");
    return buf;
}

const char *wine_dbgstr_a(const char *s)
{
    if (!s) return "(null)";
    return wine_dbg_sprintf("\"%s\"", s);
}
