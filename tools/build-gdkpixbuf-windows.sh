#!/usr/bin/env bash
# Build gdk-pixbuf for the Windows package with only its PNG loader, built in: MSYS2's has JPEG and TIFF,
# which pull in libjpeg, libtiff and through it libdeflate, libjbig, libLerc, liblzma, libwebp, libsharpyuv and
# libzstd (some 4.7 MB of DLLs), and the game reads only PNG.
#   build-gdkpixbuf-windows.sh [sysroot from windows-sysroot.py] [out dir]
# In Docker on Linux, or natively in MSYS2's MINGW64 shell (where the sysroot is MSYS2 itself and is not given).
# The source is the submodule third_party/gdk-pixbuf, at the version of the sysroot's (MSYS2's) package.
# Writes <out>/libgdk_pixbuf-2.0-0.dll, which package-windows.sh puts in place of MSYS2's.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
INV=${INV:-$ROOT/../denboku-shogi-investigation}
OPTS="--buildtype=release --strip -Ddefault_library=shared -Dpng=enabled -Djpeg=disabled -Dtiff=disabled -Dgif=disabled
  -Dglycin=disabled -Dothers=disabled -Dbuiltin_loaders=png -Dnative_windows_loaders=false -Dintrospection=disabled
  -Dman=false -Ddocumentation=false -Dtests=false -Dinstalled_tests=false -Dthumbnailer=disabled"
DLL=gdk-pixbuf/libgdk_pixbuf-2.0-0.dll
if [ "${MSYSTEM:-}" = MINGW64 ]; then
  OUT=$(realpath -m "${1:-$ROOT/build/windows-native}") B=$ROOT/build/gdk-pixbuf
  mkdir -p "$OUT"
  # shellcheck disable=SC2086
  [ -d "$B" ] || meson setup "$B" "$ROOT/third_party/gdk-pixbuf" $OPTS >"$OUT/meson.log" 2>&1 || { tail -30 "$OUT/meson.log"; exit 1; }
  ninja -C "$B" $DLL >>"$OUT/meson.log" 2>&1 || { tail -30 "$OUT/meson.log"; exit 1; }
  cp "$B/$DLL" "$OUT/" && strip "$OUT/libgdk_pixbuf-2.0-0.dll"
  exit 0
fi
SYSROOT=$(realpath "${1:-$INV/win}") OUT=$(realpath -m "${2:-$INV/win-pixbuf}")
mkdir -p "$OUT"
docker image inspect denboku-build-pixbuf >/dev/null 2>&1 ||
  printf 'FROM debian:trixie\nRUN apt-get update -qq && apt-get install -y -qq meson ninja-build pkg-config gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64 libglib2.0-dev-bin python3 && rm -rf /var/lib/apt/lists/*\n' |
  docker build -q -t denboku-build-pixbuf - >/dev/null
docker run --rm -e OPTS="$OPTS" -e DLL=$DLL -v "$ROOT/third_party/gdk-pixbuf":/src:ro -v "$SYSROOT":/sysroot:ro -v "$OUT":/out denboku-build-pixbuf sh -c '
set -e
cat > /tmp/cross.ini <<EOF
[binaries]
c = '"'"'x86_64-w64-mingw32-gcc'"'"'
ar = '"'"'x86_64-w64-mingw32-ar'"'"'
strip = '"'"'x86_64-w64-mingw32-strip'"'"'
windres = '"'"'x86_64-w64-mingw32-windres'"'"'
pkg-config = '"'"'pkg-config'"'"'
[properties]
sys_root = '"'"'/sysroot'"'"'
pkg_config_libdir = '"'"'/sysroot/mingw64/lib/pkgconfig'"'"'
[host_machine]
system = '"'"'windows'"'"'
cpu_family = '"'"'x86_64'"'"'
cpu = '"'"'x86_64'"'"'
endian = '"'"'little'"'"'
EOF
export PKG_CONFIG_SYSROOT_DIR=/sysroot
meson setup /tmp/b /src --cross-file /tmp/cross.ini $OPTS >/out/meson.log 2>&1 || { tail -30 /out/meson.log; exit 1; }
ninja -C /tmp/b $DLL >>/out/meson.log 2>&1 || { tail -30 /out/meson.log; exit 1; }
cp /tmp/b/$DLL /out/
x86_64-w64-mingw32-strip /out/libgdk_pixbuf-2.0-0.dll
x86_64-w64-mingw32-objdump -p /out/libgdk_pixbuf-2.0-0.dll | grep "DLL Name"
chown -R '"$(id -u):$(id -g)"' /out'
ls -la "$OUT"/*.dll
