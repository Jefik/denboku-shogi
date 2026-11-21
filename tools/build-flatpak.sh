#!/usr/bin/env bash
# Package the built Linux folder as a single-file Flatpak:
#   build-flatpak.sh [dist/denboku-linux] [out dir]
# Uses the prebuilt GTK 2 bundle, so no SDK or compiler is needed; only the Flatpak runtime
# org.freedesktop.Platform 24.08 (see docs/BUILD.md).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUNDLE=$(realpath "${1:-$ROOT/dist/denboku-linux}")
OUT=$(realpath -m "${2:-$ROOT/dist}")
APPID=org.denboku.shogi
RUNTIME=org.freedesktop.Platform
BRANCH=24.08
BUILD=$ROOT/build/flatpak
REPO=$BUILD/repo

command -v flatpak >/dev/null 2>&1 || { echo "flatpak not found" >&2; exit 2; }
[ -x "$BUNDLE/denboku.sh" ] || { echo "missing Linux bundle: $BUNDLE (run tools/build-linux.sh)" >&2; exit 2; }

rm -rf "$BUILD"
flatpak build-init --arch=x86_64 "$BUILD/app" "$APPID" "$RUNTIME" "$RUNTIME" "$BRANCH"
F=$BUILD/app/files
mkdir -p "$F/denboku" "$F/bin" "$F/share/applications" "$F/share/icons/hicolor/256x256/apps" "$F/share/metainfo"
cp -a "$BUNDLE"/. "$F/denboku/"
printf '#!/bin/sh\nexec /app/denboku/denboku.sh "$@"\n' > "$F/bin/denboku"
chmod 755 "$F/bin/denboku"
cp "$ROOT/data/art/icon.png" "$F/share/icons/hicolor/256x256/apps/$APPID.png"
cat > "$F/share/applications/$APPID.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Denboku Shōgi
Name[ja]=電墨将棋
Comment=Shōgi Everywhere: against the engine, on the network or on Lishogi
Exec=denboku
Icon=$APPID
Categories=Game;BoardGame;
Terminal=false
EOF
cat > "$F/share/metainfo/$APPID.metainfo.xml" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<component type="desktop-application">
  <id>$APPID</id>
  <name>Denboku Shōgi</name>
  <summary>Shōgi Everywhere: against the engine, on the network or on Lishogi</summary>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>GPL-3.0-or-later</project_license>
  <launchable type="desktop-id">$APPID.desktop</launchable>
  <url type="homepage">https://github.com/Jefik/denboku-shogi</url>
  <releases><release version="${DENBOKU_VERSION:-0.0.0}" date="$(date -u +%Y-%m-%d)"/></releases>
</component>
EOF
flatpak build-finish "$BUILD/app" --command=denboku --share=network --socket=x11 --share=ipc --device=all
flatpak build-export --arch=x86_64 "$REPO" "$BUILD/app" stable
mkdir -p "$OUT"
flatpak build-bundle --arch=x86_64 "$REPO" "$OUT/Denboku-x86_64.flatpak" "$APPID" stable
echo "flatpak: $OUT/Denboku-x86_64.flatpak"
