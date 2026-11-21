#!/usr/bin/env bash
# Build the PC version in Docker and bundle it with its GTK 2 libraries:
#   build-linux.sh [fairy-stockfish binary] [out dir] [appimage dir]
# (by default the engine and the AppImage tools in ../denboku-shogi-investigation, and dist/)
# Produces <out>/denboku-linux/ and <out>/denboku-linux.tar.xz (run ./denboku.sh), and with a
# folder holding appimagetool-x86_64.AppImage and runtime-x86_64 also <out>/Denboku-x86_64.AppImage:
# one file, with the icon. SANITIZE=1 builds it with ASan and UBSan instead (tools/asan-test.sh).
# ARCH=arm64 cross-builds the ARM64 one instead: <out>/denboku-linux-arm64/ and <out>/denboku-linux-arm64.tar.xz.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
INV=${INV:-$ROOT/../denboku-shogi-investigation}
ARCH=${ARCH:-x86_64}
ENGINE=$(realpath "${1:-$INV/fsf-build-shogi/fairy-stockfish-$ARCH}") OUT=$(realpath -m "${2:-$ROOT/dist}")
APPIMAGE=$(realpath -m "${3:-$INV/appimage}")
[ -z "${SANITIZE:-}" ] && [ "$ARCH" = x86_64 ] || APPIMAGE=/nonexistent
mkdir -p "$OUT"
# Build tools live in a cached image, so builds need no network.
case $ARCH in
  x86_64) IMAGE=denboku-build-linux T=x86_64-linux-gnu NAME=denboku-linux
    PKGS="build-essential pkg-config libgtk2.0-dev" ;;
  arm64) IMAGE=denboku-build-linux-arm64 T=aarch64-linux-gnu NAME=denboku-linux-arm64
    PKGS="crossbuild-essential-arm64 pkg-config libgtk2.0-dev:arm64" ;;
  *) echo "ARCH is x86_64 or arm64"; exit 2 ;;
esac
docker image inspect $IMAGE >/dev/null 2>&1 ||
  printf 'FROM debian:bookworm\nRUN dpkg --add-architecture arm64 && apt-get update -qq && apt-get install -y -qq %s && rm -rf /var/lib/apt/lists/*\n' "$PKGS" |
  docker build -q -t $IMAGE - >/dev/null
AI=(); [ -d "$APPIMAGE" ] && AI=(-v "$APPIMAGE":/appimage:ro)
docker run --rm -e DENBOKU_BUILD -e DENBOKU_VERSION -e SANITIZE="${SANITIZE:-}" -e T=$T -e NAME=$NAME -v "$ROOT":/src:ro -v "$ENGINE":/engine:ro -v "$OUT":/out "${AI[@]}" $IMAGE sh -c '
set -e
mkdir /tmp/src && tar -C /src -cf - Makefile src data linux | tar -C /tmp/src -xf - && cd /tmp/src
J=-j$(nproc)
M="make $J BUILD=/tmp/b CC=$T-gcc PKG_CONFIG=$T-pkg-config"
if [ -n "$SANITIZE" ]; then $M CFLAGS="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined" >/out/build.log 2>&1
else $M >/out/build.log 2>&1 && $T-strip /tmp/b/denboku; fi
D=/out/$NAME && rm -rf $D && mkdir -p $D/bin $D/lib
cp /tmp/b/denboku $D/bin/ && cp /engine $D/bin/fairy-stockfish
cp -r data/art data/fonts data/styles data/sounds data/problems data/lessons $D/
# Bundle every library the programs need, through their NEEDED entries (ldd cannot read another
# architecture), except the glibc family, which the host provides.
needed() { readelf -d "$1" 2>/dev/null | sed -n "s/.*Shared library: \[\(.*\)\]/\1/p"; }
todo="$D/bin/denboku $D/bin/fairy-stockfish" seen=" "
while [ -n "$todo" ]; do
  set -- $todo; f=$1; shift; todo="$*"
  for n in $(needed "$f"); do
    case "$seen" in *" $n "*) continue ;; esac; seen="$seen$n "
    case $n in libc.so*|libm.so*|libpthread.so*|libdl.so*|librt.so*|ld-linux*) continue ;; esac
    p=$(ls /lib/$T/$n /usr/lib/$T/$n 2>/dev/null | head -1); [ -n "$p" ] || { echo "missing $n" >&2; exit 1; }
    cp -L "$p" $D/lib/ && todo="$todo $D/lib/$n"
  done
done
# Own empty gtkrc: the host theme may need engines that are not bundled.
: > $D/gtkrc
cp linux/denboku.sh $D/denboku.sh
chmod 755 $D/denboku.sh
# The folder in the tarball is denboku-linux on every architecture.
cd /out && rm -f $NAME.tar.gz && tar cf - --transform "s,^$NAME,denboku-linux," $NAME | xz -9e -T0 > $NAME.tar.xz
if [ -d /appimage ]; then
  rm -f /out/Denboku-x86_64.AppImage
  A=/tmp/AppDir && cp -r $D $A && cp /src/data/art/icon.png $A/denboku.png
  printf "#!/bin/sh\nD=\$(dirname \"\$(readlink -f \"\$0\")\")\nexec \"\$D/denboku.sh\" \"\$@\"\n" > $A/AppRun && chmod 755 $A/AppRun
  printf "[Desktop Entry]\nType=Application\nName=Denboku Shōgi\nName[ja]=電墨将棋\nComment=Shōgi Everywhere: against the engine, on the network or on Lishogi\nExec=denboku\nIcon=denboku\nCategories=Game;BoardGame;\nTerminal=false\n" > $A/denboku.desktop
  ARCH=x86_64 /appimage/appimagetool-x86_64.AppImage --appimage-extract-and-run --runtime-file /appimage/runtime-x86_64 \
    --comp zstd --mksquashfs-opt -Xcompression-level --mksquashfs-opt 22 --mksquashfs-opt -b --mksquashfs-opt 1M \
    $A /out/Denboku-x86_64.AppImage >/out/appimage.log 2>&1
fi
chown -R '"$(id -u):$(id -g)"' /out'
ls -la "$OUT"
