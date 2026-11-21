#!/usr/bin/env bash
# Cross build the Windows version in Docker and bundle it with its GTK 2 DLLs:
#   build-windows.sh [sysroot from windows-sysroot.py] [fairy-stockfish .exe] [out dir]
# (by default the sysroot and the engine in ../denboku-shogi-investigation, and dist/)
# Produces <out>/denboku-windows/ (start Denboku.exe) and <out>/denboku-windows.zip.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
INV=${INV:-$ROOT/../denboku-shogi-investigation}
SYSROOT=$(realpath "${1:-$INV/win}") ENGINE=$(realpath "${2:-$INV/fsf-build-shogi/fairy-stockfish-x86_64.exe}")
OUT=$(realpath -m "${3:-$ROOT/dist}")
# gdk-pixbuf with only the PNG loader (tools/build-gdkpixbuf-windows.sh), made once.
PIXBUF=$INV/win-pixbuf/libgdk_pixbuf-2.0-0.dll
[ -f "$PIXBUF" ] || "$ROOT/tools/build-gdkpixbuf-windows.sh" "$SYSROOT" "$INV/win-pixbuf"
mkdir -p "$OUT"
# Build tools live in a cached image, so builds need no network.
docker image inspect denboku-build-windows >/dev/null 2>&1 ||
  printf 'FROM debian:bookworm\nRUN apt-get update -qq && apt-get install -y -qq make pkg-config gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64 zip && rm -rf /var/lib/apt/lists/*\n' |
  docker build -q -t denboku-build-windows - >/dev/null
docker run --rm -e DENBOKU_BUILD -e DENBOKU_VERSION -v "$ROOT":/src:ro -v "$SYSROOT":/sysroot:ro -v "$ENGINE":/engine.exe:ro -v "$PIXBUF":/pixbuf.dll:ro -v "$OUT":/out denboku-build-windows sh -c '
set -e
mkdir /tmp/src && tar -C /src -cf - Makefile src data windows tools/package-windows.sh | tar -C /tmp/src -xf - && cd /tmp/src
make -j$(nproc) windows SYSROOT=/sysroot BUILD=/tmp/b >/out/build-windows.log 2>&1
bash tools/package-windows.sh /tmp/b /sysroot/mingw64 /engine.exe /pixbuf.dll /out
chown -R '"$(id -u):$(id -g)"' /out'
du -sh "$OUT/denboku-windows" "$OUT/denboku-windows.zip"
