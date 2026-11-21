#!/usr/bin/env bash
# Build Fairy-Stockfish (9x9 needs largeboards) in Docker:
#   build-engine.sh <Fairy-Stockfish source> <out dir>
# Writes fairy-stockfish-armv7 (static, NEON, for the Kindle), fairy-stockfish-x86_64, fairy-stockfish-arm64 and
# fairy-stockfish-x86_64.exe (static),
# with tools/fsf-shogi-only.patch: about 18 MB less memory (the unused tables given back), a pawn table of 2 MB instead of 18 and a 4 MB hash from the start, which the Kindle is short of.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=$(realpath "$1") OUT=$(realpath -m "$2")
mkdir -p "$OUT"
docker run --rm -v "$SRC":/src:ro -v "$ROOT/tools/fsf-shogi-only.patch":/shogi.patch:ro -v "$OUT":/out debian:bookworm sh -c '
set -e
apt-get update -qq >/dev/null && apt-get install -y -qq g++ make patch g++-arm-linux-gnueabihf g++-aarch64-linux-gnu g++-mingw-w64-x86-64-posix >/dev/null
cp -r /src /tmp/fsf && cd /tmp/fsf && patch -p1 < /shogi.patch >/dev/null && cd src && make clean >/dev/null
make -j"$(nproc)" build ARCH=x86-64 largeboards=yes COMP=gcc EXTRACXXFLAGS=-DSHOGI_ONLY EXTRALDFLAGS=-static >/out/x86_64.log 2>&1
strip stockfish && cp stockfish /out/fairy-stockfish-x86_64
make clean >/dev/null
make -j"$(nproc)" build ARCH=armv7-neon largeboards=yes COMP=gcc COMPCXX=arm-linux-gnueabihf-g++ EXTRACXXFLAGS=-DSHOGI_ONLY EXTRALDFLAGS=-static >/out/armv7.log 2>&1
arm-linux-gnueabihf-strip stockfish && cp stockfish /out/fairy-stockfish-armv7
make clean >/dev/null
make -j"$(nproc)" build ARCH=armv8 largeboards=yes COMP=gcc COMPCXX=aarch64-linux-gnu-g++ EXTRACXXFLAGS=-DSHOGI_ONLY EXTRALDFLAGS=-static >/out/arm64.log 2>&1
aarch64-linux-gnu-strip stockfish && cp stockfish /out/fairy-stockfish-arm64
make clean >/dev/null
make -j"$(nproc)" build ARCH=x86-64 largeboards=yes COMP=mingw EXTRACXXFLAGS=-DSHOGI_ONLY EXTRALDFLAGS=-static >/out/windows.log 2>&1
x86_64-w64-mingw32-strip stockfish.exe && cp stockfish.exe /out/fairy-stockfish-x86_64.exe
chown -R '"$(id -u):$(id -g)"' /out'
ls -la "$OUT"
