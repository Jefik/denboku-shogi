#!/usr/bin/env bash
# Build Fairy-Stockfish (9x9 needs largeboards) in Docker:
#   build-engine.sh <Fairy-Stockfish source> <out dir> [target...]
# The source is Fairy-Stockfish at FSF_COMMIT (the CI clones it). Targets (all by default): armv7 (fairy-stockfish-armv7, static, NEON,
# for the Kindle), x86_64, arm64 and windows (fairy-stockfish-x86_64.exe, static),
# with tools/fsf-shogi-only.patch: about 18 MB less memory (the unused tables given back), a pawn table of 2 MB instead of 18 and a 4 MB hash from the start, which the Kindle is short of.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
FSF_COMMIT=2f1959e1f0b44d4648ff35015b89d8e918a5c746
SRC=$(realpath "$1") OUT=$(realpath -m "$2")
shift 2
TARGETS=${*:-x86_64 armv7 arm64 windows}
[ "$(git -C "$SRC" rev-parse HEAD 2>/dev/null)" = $FSF_COMMIT ] || echo "warning: $SRC is not Fairy-Stockfish $FSF_COMMIT" >&2
mkdir -p "$OUT"
docker run --rm -e TARGETS="$TARGETS" -v "$SRC":/src:ro -v "$ROOT/tools/fsf-shogi-only.patch":/shogi.patch:ro -v "$OUT":/out debian:bookworm sh -c '
set -e
apt-get update -qq >/dev/null && apt-get install -y -qq g++ make patch g++-arm-linux-gnueabihf g++-aarch64-linux-gnu g++-mingw-w64-x86-64-posix >/dev/null
cp -r /src /tmp/fsf && cd /tmp/fsf && patch -p1 < /shogi.patch >/dev/null && cd src
# make <name> <out file> <strip> <make options...>
build() {
  name=$1 file=$2 strip=$3; shift 3
  make clean >/dev/null
  make -j"$(nproc)" build largeboards=yes EXTRACXXFLAGS=-DSHOGI_ONLY EXTRALDFLAGS=-static "$@" >/out/$name.log 2>&1
  $strip stockfish* && cp stockfish* /out/$file
}
for t in $TARGETS; do
  case $t in
    x86_64) build x86_64 fairy-stockfish-x86_64 strip ARCH=x86-64 COMP=gcc ;;
    armv7) build armv7 fairy-stockfish-armv7 arm-linux-gnueabihf-strip ARCH=armv7-neon COMP=gcc COMPCXX=arm-linux-gnueabihf-g++ ;;
    arm64) build arm64 fairy-stockfish-arm64 aarch64-linux-gnu-strip ARCH=armv8 COMP=gcc COMPCXX=aarch64-linux-gnu-g++ ;;
    windows) build windows fairy-stockfish-x86_64.exe x86_64-w64-mingw32-strip ARCH=x86-64 COMP=mingw ;;
    *) echo "unknown target $t"; exit 1 ;;
  esac
done
chown -R '"$(id -u):$(id -g)"' /out'
ls -la "$OUT"
