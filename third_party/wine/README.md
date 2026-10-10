# Vendored Wine sources

Unmodified files from [Wine](https://www.winehq.org/), taken from the
[wine-mirror/wine](https://github.com/wine-mirror/wine) repository at commit
`485aecb8c06e8436696a76dfe263cd05d77d3d9c` (2026-04-15).

| File | Use |
|---|---|
| `include/wintab.h`, `include/pktdef.h`, `include/wintabx.h` | Wintab API headers, used as-is |
| `dlls/wintab32/manager.c` | `WTMgr*` stubs, compiled as-is |
| `dlls/wintab32/context.c`, `wintab32.c`, `wintab_internal.h` | Reference copies; the adapted versions live in `src/wintab/` |
| `dlls/winex11.drv/wintab.c` | Reference for `src/wintab/backend_otd.c` (X11 backend, not compiled) |
| `dlls/wintab32/wintab32.spec` | Export ordinals, mirrored in `src/wintab/wintab32.def` |

The pristine copies are kept so that our changes can be reviewed with a plain
diff, e.g. `git diff --no-index third_party/wine/dlls/wintab32/context.c src/wintab/context.c`.

Wine is licensed under the GNU LGPL 2.1 or later; see `COPYING.LIB` and the
copyright headers of the individual files.
