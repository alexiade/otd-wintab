/*
 * otd-wintab: minimal stand-ins for the Wine build environment, so the
 * Wine wintab32 sources build as a native Windows DLL. Force-included
 * into every translation unit of the provider (see build.ps1).
 *
 * Copyright 2026 otd-wintab contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef OTDWT_WINE_COMPAT_H
#define OTDWT_WINE_COMPAT_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/* wintab.h declares A/W pairs through these Winelib helpers */
#define DECL_WINELIB_TYPE_AW(type)  typedef type##W type;
#define WINELIB_NAME_AW(func)       func##W

/* Logging. Enabled by setting OTD_WINTAB_LOG to a file path. */
extern int otdwt_log_enabled;
void otdwt_log(const char *level, const char *func, const char *fmt, ...);
const char *wine_dbg_sprintf(const char *fmt, ...);
const char *wine_dbgstr_w(const WCHAR *s);
const char *wine_dbgstr_a(const char *s);
#define debugstr_a wine_dbgstr_a
#define debugstr_w wine_dbgstr_w

#define WINE_DEFAULT_DEBUG_CHANNEL(ch)
#define TRACE_ON(ch)  (otdwt_log_enabled)
#define OTDWT_LOG_(lvl, ...) \
    do { if (otdwt_log_enabled) otdwt_log(lvl, __func__, __VA_ARGS__); } while (0)
#define TRACE(...)  OTDWT_LOG_("trace", __VA_ARGS__)
#define WARN(...)   OTDWT_LOG_("warn",  __VA_ARGS__)
#define FIXME(...)  OTDWT_LOG_("fixme", __VA_ARGS__)
#define ERR(...)    OTDWT_LOG_("err",   __VA_ARGS__)

#endif /* OTDWT_WINE_COMPAT_H */
