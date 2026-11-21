#!/usr/bin/env bash
# Bundle a Windows build with its GTK 2 DLLs; build-windows.sh runs it in Docker, build-windows-native.sh in MSYS2:
#   package-windows.sh <build dir of make windows> <mingw64 dir> <fairy-stockfish .exe> <gdk-pixbuf DLL> <out dir>
# Writes <out>/denboku-windows/ (start Denboku.exe) and <out>/denboku-windows.zip. WIN_PREFIX is the
# tools' prefix (x86_64-w64-mingw32- by default; empty in MSYS2).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=$1 M=$2 ENGINE=$3 PIXBUF=$4 OUT=$5
P=${WIN_PREFIX-x86_64-w64-mingw32-}
${P}gcc -O2 -mwindows -municode -s -o "$B/Denboku.exe" "$ROOT/windows/launcher.c" "$B/windows/icon.o"
D=$OUT/denboku-windows
rm -rf "$D" && mkdir -p "$D/bin" "$D/etc/gtk-2.0"
cp "$B/Denboku.exe" "$D/" && cp "$B/windows/denboku.exe" "$D/bin/" && cp "$ENGINE" "$D/bin/fairy-stockfish.exe"
cp "$M/bin/gspawn-win64-helper.exe" "$M/bin/gspawn-win64-helper-console.exe" "$D/bin/"
# The plain GTK look, as on Linux and the Kindle: the native MS-Windows theme draws its own
# backgrounds and ignores the dark palettes.
: >"$D/etc/gtk-2.0/gtkrc"
(cd "$ROOT/data" && cp -r art fonts styles sounds problems lessons "$D/")
# Every DLL the programs need that MSYS2 has (the rest are part of Windows).
cp "$PIXBUF" "$D/bin/libgdk_pixbuf-2.0-0.dll"
todo="$D/bin/denboku.exe $D/bin/gspawn-win64-helper.exe $D/bin/libgdk_pixbuf-2.0-0.dll"
while [ -n "$todo" ]; do
  next=""
  for f in $todo; do
    for dll in $(${P}objdump -p "$f" | awk '/DLL Name:/ {print $3}'); do
      [ -e "$D/bin/$dll" ] && continue
      [ -e "$M/bin/$dll" ] || continue
      cp "$M/bin/$dll" "$D/bin/" && next="$next $D/bin/$dll"
    done
  done
  todo=$next
done
rm -f "$OUT/denboku-windows.zip" && (cd "$D" && zip -qr9 "$OUT/denboku-windows.zip" .)
