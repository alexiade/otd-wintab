#!/bin/bash
# otd-wintab: build GTK for Windows in MSYS2 CLANG64, to test it with
# scripts/test-gtk.ps1 or scripts/test-gtk4.ps1.
#
#   build-gtk.sh 3|4 <GTK source dir> <build dir>
#
# Only the libraries are built (no docs, demos, tests or introspection),
# with GDK_DEBUG=input available. Prints the directories to pass to the
# test scripts' -GtkOverride. See docs/gtk-testing.md.
export MSYSTEM=CLANG64
source /etc/profile || true     # not set -e safe
set -e

if [ $# -ne 3 ] || { [ "$1" != 3 ] && [ "$1" != 4 ]; }; then
  echo "usage: $0 3|4 <GTK source dir> <build dir>" >&2
  exit 2
fi
MAJOR="$1"
SRC="$(cygpath -u "$2")"
BUILD="$(cygpath -u "$3")"

if [ "$MAJOR" = 3 ]; then
  export CFLAGS="-DG_ENABLE_DEBUG -Wno-deprecated-declarations"
  if [ ! -f "$BUILD/build.ninja" ]; then
    meson setup "$BUILD" "$SRC" --wrap-mode=nodownload --buildtype=release \
      -Dwin32_backend=true -Dbroadway_backend=false \
      -Dintrospection=false -Dgtk_doc=false -Dman=false \
      -Ddemos=false -Dexamples=false -Dtests=false -Dinstalled_tests=false
  fi
  meson compile -C "$BUILD" gdk-3 gtk-3
  dirs="$BUILD/gdk $BUILD/gtk"
else
  export CFLAGS="-DG_ENABLE_DEBUG"
  if [ ! -f "$BUILD/build.ninja" ]; then
    meson setup "$BUILD" "$SRC" --wrap-mode=nodownload --buildtype=release \
      -Dwin32-backend=true -Dbroadway-backend=false -Dmedia-gstreamer=disabled \
      -Dprint-cups=disabled -Dprint-cpdb=disabled -Dvulkan=disabled \
      -Dintrospection=disabled -Ddocumentation=false -Dman-pages=false \
      -Dbuild-demos=false -Dbuild-testsuite=false -Dbuild-examples=false \
      -Dbuild-tests=false -Daccesskit=disabled -Dsysprof=disabled
  fi
  ninja -C "$BUILD" gtk/libgtk-4-1.dll
  dirs="$BUILD/gtk"
fi

echo
echo "GTK $MAJOR built. Test it with -GtkOverride:"
for d in $dirs; do echo "  $(cygpath -w "$d")"; done
