#!/usr/bin/env bash
# The MSIX's folder from the portable one (tools/package-windows.sh), for makeappx pack on Windows
# (.github/workflows/windows.yml):
#   package-msix.sh <denboku-windows folder> <out folder> <version> <publisher>
# The version is the release's (0.9.1 → 0.9.1.0); the publisher is the signing certificate's subject, exactly.
# Needs rsvg-convert (librsvg) for the logos, drawn from data/art/icon.svg.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=$1 OUT=$2 VERSION=$3 PUBLISHER=$4
[[ $VERSION =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "version $VERSION is not x.y.z"; exit 1; }
rm -rf "$OUT" && mkdir -p "$OUT" && cp -r "$SRC"/. "$OUT/" && mkdir "$OUT/Assets"
for logo in StoreLogo:50 Square44x44Logo:44 Square150x150Logo:150; do
  rsvg-convert -w "${logo#*:}" -h "${logo#*:}" "$ROOT/data/art/icon.svg" -o "$OUT/Assets/${logo%:*}.png"
done
esc() { sed 's/&/\&amp;/g; s/"/\&quot;/g; s/</\&lt;/g'; }
sed -e "s|@VERSION@|$VERSION.0|" -e "s|@PUBLISHER@|$(printf %s "$PUBLISHER" | esc | sed 's/[|\\]/\\&/g')|" \
  "$ROOT/windows/AppxManifest.xml" >"$OUT/AppxManifest.xml"
