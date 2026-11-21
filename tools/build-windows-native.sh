#!/usr/bin/env bash
# The Windows package built on Windows itself, in MSYS2's MINGW64 shell (https://www.msys2.org), with no
# Docker or WSL; tools\build-windows.ps1 runs it from PowerShell, in an MSYS2 of its own:
#   tools/build-windows-native.sh
# Installs what it needs with pacman (GTK 2 is no longer in MSYS2's database: its last package comes from
# the mirror), builds Fairy-Stockfish (shōgi only), gdk-pixbuf with only PNG and the game, and writes
# dist/denboku-windows/ (start Denboku.exe) and dist/denboku-windows.zip, as build-windows.sh does on Linux.
set -euo pipefail
[ "${MSYSTEM:-}" = MINGW64 ] || { echo "run it in MSYS2's MINGW64 shell"; exit 1; }
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=$ROOT/build/windows-native
M=mingw-w64-x86_64
FSF=2f1959e1f0b44d4648ff35015b89d8e918a5c746 # the Fairy-Stockfish commit of the other builds
mkdir -p "$W" "$ROOT/dist"
pacman -S --needed --noconfirm make git zip patch $M-gcc $M-pkgconf $M-meson $M-ninja
pacman -Q $M-gtk2 >/dev/null 2>&1 || pacman -U --noconfirm https://repo.msys2.org/mingw/mingw64/$M-gtk2-2.24.33-8-any.pkg.tar.zst
# The submodule; from a zip of the repo (no .git), its tag (the commit `git submodule status` shows).
if [ ! -f "$ROOT/third_party/gdk-pixbuf/meson.build" ]; then
  if [ -d "$ROOT/.git" ]; then git -C "$ROOT" submodule update --init --depth 1 third_party/gdk-pixbuf
  else git clone -q --depth 1 --branch 2.44.8 https://gitlab.gnome.org/GNOME/gdk-pixbuf.git "$ROOT/third_party/gdk-pixbuf"; fi
fi

if [ ! -f "$W/fairy-stockfish.exe" ]; then
  echo "engine..."
  rm -rf "$W/fsf" && git init -q "$W/fsf"
  git -C "$W/fsf" fetch -q --depth 1 https://github.com/fairy-stockfish/Fairy-Stockfish.git $FSF
  git -C "$W/fsf" checkout -q FETCH_HEAD && patch -s -d "$W/fsf" -p1 <"$ROOT/tools/fsf-shogi-only.patch"
  make -C "$W/fsf/src" -j"$(nproc)" build ARCH=x86-64 largeboards=yes COMP=mingw EXTRACXXFLAGS=-DSHOGI_ONLY \
    EXTRALDFLAGS=-static >"$W/engine.log" 2>&1 || { tail -20 "$W/engine.log"; exit 1; }
  strip "$W/fsf/src/stockfish.exe" && cp "$W/fsf/src/stockfish.exe" "$W/fairy-stockfish.exe"
fi
echo "gdk-pixbuf..."
"$ROOT/tools/build-gdkpixbuf-windows.sh" "$W"
echo "game..."
make -C "$ROOT" -j"$(nproc)" windows-native BUILD="$W/game" >"$W/game.log" 2>&1 || { tail -20 "$W/game.log"; exit 1; }
WIN_PREFIX= "$ROOT/tools/package-windows.sh" "$W/game" /mingw64 "$W/fairy-stockfish.exe" "$W/libgdk_pixbuf-2.0-0.dll" "$ROOT/dist"
du -sh "$ROOT/dist/denboku-windows" "$ROOT/dist/denboku-windows.zip"
