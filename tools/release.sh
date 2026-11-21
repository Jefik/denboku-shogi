#!/usr/bin/env bash
# The update manifest of a release: dist/update.txt (each package's size and BLAKE2b-256) and its signature,
# dist/update.txt.sig, from the packages in dist/ (tools/build.sh --release). Upload both with the packages.
#   release.sh <build> [version]   e.g. release.sh 20261116.1624 0.9.0
# KEY=<secret key> (default ../denboku-shogi-investigation/keys/update-main.key).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD_NO=${1:?build number, as in About} VERSION=${2:-}
KEY=${KEY:-$ROOT/../denboku-shogi-investigation/keys/update-main.key}
cd "$ROOT"
make -s sign-release
SIGN=build/sign-release
OUT=dist/update.txt
{
  echo "[Release]"
  echo "Build=$BUILD_NO"
  [ -n "$VERSION" ] && echo "Version=$VERSION" && echo "Tag=v$VERSION" && echo "Notes=https://github.com/Jefik/denboku-shogi/releases/tag/v$VERSION"
  for pair in kindle:denboku-kindle.zip linux-x86_64:denboku-linux.tar.xz linux-arm64:denboku-linux-arm64.tar.xz \
              appimage-x86_64:Denboku-x86_64.AppImage windows-x86_64:denboku-windows.zip android:Denboku.apk android-x86_64:Denboku-x86_64.apk; do
    platform=${pair%%:*} file=${pair#*:}
    [ -f "dist/$file" ] || { echo "skipped $platform: no dist/$file" >&2; continue; }
    # A package of another build would be offered as this one, and installed again every day.
    [ "$(cat "dist/$file.build" 2>/dev/null)" = "$BUILD_NO" ] || { echo "dist/$file is not build $BUILD_NO (dist/$file.build)" >&2; exit 1; }
    read -r size hash < <($SIGN hash "dist/$file")
    printf '\n[%s]\nFile=%s\nSize=%s\nBlake2b=%s\n' "$platform" "$file" "$size" "$hash"
  done
} >"$OUT"
$SIGN sign "$KEY" "$OUT"
echo "$OUT and $OUT.sig"
