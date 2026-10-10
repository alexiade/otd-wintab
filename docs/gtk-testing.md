# Testing GTK's Wintab support

otd-wintab includes two small GTK applications that check GTK's Wintab
input from an application's point of view, without a tablet and without
GIMP. They were written to find and verify fixes for GTK itself, and they
work with any GTK 3 or GTK 4 build: a release, MSYS2's packages, or your
own build of a branch.

- `tests/gtk/gtkprobe.c` (GTK 3) does what GIMP 3 does with input
  devices: it enumerates them through the seat, enables pens, maps events
  to devices through their source device (falling back from a disabled
  slave to its associated device), and grabs the canvas for a stroke.
- `tests/gtk4/gtk4probe.c` (GTK 4) does what GTK 4 drawing applications
  do: a `GtkGestureStylus` on the canvas, with the pen identified by the
  event's `GdkDeviceTool`.

Both act as the tablet driver themselves. They create otd-wintab's ring
(the shared memory that OpenTabletDriver normally writes to), so the
otd-wintab Wintab provider loaded into the probe reports scripted pens:
a grip pen, its eraser end, an airbrush with a finger wheel and an Art
Pen with barrel rotation. As a real driver with `CXO_SYSTEM` would, the
probes also move the system cursor along with the pen and hold the left
mouse button while the tip is down, using `SendInput`. Then they check
what the application saw after each step:

| Step | Checks |
|---|---|
| mouse | the mouse is the current device |
| grip pen stroke | the pen is current; press, release and pressure arrive; no mouse events while the tip is down; the stroke stays on the pen |
| pen lifted, mouse moves | the mouse is current again |
| airbrush stroke | the airbrush is current, and its wheel arrives |
| back to the grip pen | the same pen (GTK 4: the same `GdkDeviceTool`) |
| pen leaves with tip down, then mouse | no stroke or grab is left behind |
| eraser end | the eraser is current |
| two strokes in one proximity | the second stroke still has pressure |
| art pen twisting | rotation changes while hovering and with the tip down |
| seat (GTK 4) | the seat lists and announces the pens and their tools |

The probes take over the mouse for about ten seconds; don't touch it
while they run. They expect a display scale of 100%.

## Requirements

- Windows 10 or 11.
- otd-wintab built: `.\build.ps1` (fetches Zig on first use; also needs
  the .NET 8 SDK for the OpenTabletDriver plugin).
- To test a GTK build: [MSYS2](https://www.msys2.org) with the CLANG64
  environment and GTK's build dependencies, for instance

  ```sh
  P=mingw-w64-clang-x86_64
  pacman -S --needed $P-toolchain $P-meson $P-ninja $P-pkgconf $P-python \
    $P-gtk3 $P-gtk4 $P-sassc $P-gettext-tools $P-shaderc $P-vulkan-headers
  ```

  Installing `gtk3` and `gtk4` pulls in every library GTK needs, and the
  probes run on that runtime, with your build in front of it.

The probes select GTK's Wintab backend with
`GDK_WIN32_TABLET_INPUT_API=wintab`: GTK 4, and current GTK 3.24, use
Windows Ink by default.

## Testing a released GTK

```powershell
.\scripts\test-gtk.ps1 -NoGlue -GtkBin C:\msys64\clang64\bin   # MSYS2's GTK 3
.\scripts\test-gtk.ps1 -NoGlue                                 # GIMP 3's GTK 3
.\scripts\test-gtk4.ps1                                        # MSYS2's GTK 4
```

`-NoGlue` matters for GTK 3. Without it, otd-wintab works around GTK 3's
Wintab bugs inside the application, which is what it does for GIMP, and
the test then checks the workarounds rather than GTK. There are no such
workarounds for GTK 4.

## Testing a GTK build

Build the libraries from a checkout, for instance a merge request's branch:

```powershell
git clone https://gitlab.gnome.org/GNOME/gtk.git S:\src\gtk
git -C S:\src\gtk worktree add S:\src\gtk-3 gtk-3-24

.\scripts\build-gtk.ps1 -Gtk 3 -Source S:\src\gtk-3 -Build S:\src\gtk-3-build
.\scripts\build-gtk.ps1 -Gtk 4 -Source S:\src\gtk -Build S:\src\gtk-4-build
```

This builds only the libraries (no docs, demos, tests or introspection),
with `GDK_DEBUG` support, and prints the directories with the new DLLs.
Run the probes with those in front of MSYS2's runtime:

```powershell
.\scripts\test-gtk.ps1 -NoGlue -GtkBin C:\msys64\clang64\bin `
    -GtkOverride S:\src\gtk-3-build\gdk, S:\src\gtk-3-build\gtk
.\scripts\test-gtk4.ps1 -GtkOverride S:\src\gtk-4-build\gtk
```

The scripts print which `libgdk-3-0.dll` / `libgtk-4-1.dll` they use, so
you can check that your build was picked up. Both exit with 0 when all
checks pass.

## Reading the results

Each step prints the device or tool the application considers current,
and `ok` or `FAILED`. For a failed check, the events the canvas received
during that step are printed, collapsed into runs, for instance:

```
grip pen stroke                              current: (mouse)        FAILED
        4x motion   (mouse)        p=-1,00 rot=-1,00
        1x press    (mouse)        p=-1,00 rot=-1,00  [os button]
```

Each line gives the event type, the device or tool (GTK 3: the event's
source device and the axes of its device; GTK 4: the event's tool),
pressure and other axes, and whether the probe was holding the mouse
button. `(mouse)` and `p=-1` on GTK 4 mean the event had no tool and no
axes: it came from the mouse, not the pen.

For more detail:

- `OTD_GTKPROBE_VERBOSE=1` prints every event as it arrives.
- `OTD_GTKPROBE_NO_OSBUTTON=1` stops the probes from pressing the mouse
  button, to tell apart problems caused by the mouse messages that a
  real driver generates.
- `$env:GDK_DEBUG = 'input'` (or `'input,events'`) makes GTK print its
  Wintab context, the devices it creates, and the messages it handles.
- `-Log` writes the Wintab provider's trace to
  `build\gtk-test\gtkprobe-wintab.log` / `build\gtk4-test\gtk4probe-wintab.log`.

## Results

With the fixes proposed for GTK in 2026-10 (branches
`wintab-device-fixes` on gtk-3-24 and `wintab-device-fixes-gtk4` on main):

| GTK | Unpatched | Patched |
|---|---|---|
| 3.24.52 | 9 failed checks | all pass |
| 4.25 (main) | 25 failed checks: Wintab doesn't start | all pass |
