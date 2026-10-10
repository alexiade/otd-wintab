# otd-wintab

A Wintab provider for [OpenTabletDriver](https://opentabletdriver.net), so
Wintab applications such as GIMP get **one input device per physical
stylus**, the way Linux has always done it, with pressure, tilt, eraser,
airbrush wheel and Art Pen rotation.

Windows Ink can't do this: it has no notion of tool identity, so every pen
on every tablet is the same device. Wintab can: each cursor has a type, a
physical ID (serial), and its own axes. otd-wintab creates a Wintab cursor
the first time it sees a stylus, named after the tool and its serial, e.g.

    OpenTabletDriver Airbrush #0F1A2B3C
    OpenTabletDriver Grip Pen Eraser #0F1A2B3D

Because the device name depends only on the stylus, an application's
per-device settings follow the pen from one tablet to another.

> **Status:** early. The Wintab provider, the GIMP injection and a
> hardware-free test suite work. The OpenTabletDriver output plugin that
> feeds real tablets is next; until then the provider can be tried with a
> built-in fake tablet that follows the mouse.

## How it works

```
tablet ─► OpenTabletDriver ─► otd-wintab plugin ──(shared memory ring)──► otd-wintab32.dll
                                                                           (inside the app)
```

* **`otd-wintab32.dll`** is a Wintab implementation adapted from
  [Wine's wintab32](third_party/wine/README.md). Wine's generic context,
  queue and message handling are kept; the X11 backend is replaced by one
  that reads tool events from a shared-memory ring and creates cursors on
  the fly. GTK picks new cursors up via `WT_CSRCHANGE`.
* **The OpenTabletDriver plugin** (to come) publishes proximity and packet
  events, including tool id and serial, into the ring. The format is in
  [`include/otdwt_ring.h`](include/otdwt_ring.h).
* **Per-application install.** GTK loads Wintab from the system directory
  by absolute path, so the provider is injected instead: a `dwmapi.dll`
  proxy placed next to the application forwards all of `dwmapi` to the
  system copy and redirects GTK's `Wintab32.dll` load to
  `otd-wintab32.dll`. The system Wintab (e.g. Wacom's driver) stays in
  place for every other application.

Axes as GIMP sees them:

| Tablet data | Wintab | GTK / GIMP |
|---|---|---|
| pressure | `pkNormalPressure` | Pressure |
| tilt | `pkOrientation` azimuth/altitude | X tilt, Y tilt |
| airbrush finger wheel | `pkTangentPressure` | Wheel |
| Art Pen barrel rotation | `pkOrientation.orTwist` | Wheel |
| eraser end | separate cursor, `TPS_INVERT` | separate device |

## Building

Requires Windows and PowerShell. The build fetches a portable
[Zig](https://ziglang.org) toolchain into `tools/zig` on first run (checksum
verified); nothing is installed system-wide.

```powershell
.\build.ps1          # build into .\build
.\build.ps1 -Test    # build and run all tests
```

## Tests

The tests need no tablet, no OpenTabletDriver and no GIMP:

* **`selftest.exe`** plays the producer side of the ring and drives the
  provider through its public Wintab exports, opening contexts exactly as
  GTK 3 does. Each case runs in a fresh process on a private ring
  (`OTD_WINTAB_INSTANCE`), so a live setup on the same machine is never
  disturbed. Covers `WTInfo` (incl. ANSI conversion), dynamic cursor
  creation and naming, per-tool capabilities, pressure/tilt/wheel/rotation
  and eraser encoding, proximity, coordinate mapping for GTK-style and
  classic contexts, multiple contexts, enable/disable, queue overflow and
  draining, and ring attach/overrun/restart behaviour.
* **`inject_host.exe`** reproduces GIMP's module layout with a mock
  `libgdk-3-0.dll` and checks the `dwmapi` proxy: export forwarding (named
  and ordinal-only), Wintab redirection, pass-through of other loads, and
  the `OTD_WINTAB_DISABLE` switch.

Run one provider test on its own with
`build\selftest.exe build\otd-wintab32.dll <test-name>`.

## Trying it in GIMP

```powershell
.\scripts\install-gimp.ps1            # GIMP must be closed
```

In GIMP, set *Edit → Preferences → Input Devices → Pointer Input API* to
**Wintab** and restart GIMP. Without the OpenTabletDriver plugin you can use
the fake tablet:

```powershell
$env:OTD_WINTAB_FAKE = 1
& "$env:LOCALAPPDATA\Programs\GIMP 3\bin\gimp-3.2.exe"
```

The fake tablet follows the mouse. Hold the left button to draw with
pulsing pressure; **Caps Lock** switches to an airbrush (wheel), **Scroll
Lock** to an Art Pen (rotation), **Shift** selects the eraser end. Each
becomes its own device in *Edit → Input Devices*.

Remove with `.\scripts\install-gimp.ps1 -Uninstall`.

## Environment variables

| Variable | Effect |
|---|---|
| `OTD_WINTAB_LOG=<file>` | provider trace log |
| `OTD_WINTAB_FAKE=1` | built-in fake tablet that follows the mouse |
| `OTD_WINTAB_DISABLE=1` | proxy leaves Wintab loading alone |
| `OTD_WINTAB_INSTANCE=<name>` | use a private ring (tests) |

## License

GNU LGPL 2.1 or later, like the Wine code it is built on. See
[COPYING](COPYING).
