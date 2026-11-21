#!/usr/bin/env bash
# Build Fairy-Stockfish for the Android ABIs, as tools/build-engine.sh does for the others
# (largeboards, tools/fsf-shogi-only.patch): build-engine-android.sh <out dir>
# Writes <out>/<abi>/libfairystockfish.so; the source is in ../denboku-shogi-investigation.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(realpath -m "$1")
INV=${INV:-$ROOT/../denboku-shogi-investigation}
NDK=${ANDROID_NDK:-${ANDROID_HOME:-$HOME/Android/Sdk}/ndk/27.2.12479018}
export PATH="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH"
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
cp -r "$INV/src/Fairy-Stockfish" "$W/fsf"
(cd "$W/fsf" && patch -p1 -s < "$ROOT/tools/fsf-shogi-only.patch")
build() { # build <abi> <arch> <cxx>
  make -C "$W/fsf/src" -s clean >/dev/null 2>&1
  make -C "$W/fsf/src" -j"$(nproc)" build ARCH="$2" COMP=ndk OS=Android largeboards=yes CXX="$3" \
    EXTRACXXFLAGS=-DSHOGI_ONLY >"$W/$1.log" 2>&1 || { tail "$W/$1.log"; exit 1; }
  mkdir -p "$OUT/$1"
  llvm-strip "$W/fsf/src/stockfish" -o "$OUT/$1/libfairystockfish.so"
}
build arm64-v8a armv8 aarch64-linux-android24-clang++
build armeabi-v7a armv7-neon armv7a-linux-androideabi24-clang++
build x86_64 x86-64 x86_64-linux-android24-clang++
