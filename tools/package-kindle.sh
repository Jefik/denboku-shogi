#!/usr/bin/env bash
# Assemble the KUAL extension: package-kindle.sh [denboku binary] [fairy-stockfish binary] [out dir]
# (by default build/kindle/denboku, the armv7 engine in ../denboku-shogi-investigation, and dist/)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
INV=${INV:-$ROOT/../denboku-shogi-investigation}
APP=${1:-$ROOT/build/kindle/denboku} ENGINE=${2:-$INV/fsf-build-shogi/fairy-stockfish-armv7} OUT=${3:-$ROOT/dist}/denboku
rm -rf "$OUT"
mkdir -p "$OUT/bin"
cp "$APP" "$OUT/bin/denboku"
cp "$ENGINE" "$OUT/bin/fairy-stockfish"
cp -r "$ROOT/data/art" "$ROOT/data/fonts" "$ROOT/data/styles" "$ROOT/data/sounds" "$ROOT/data/problems" "$ROOT/data/lessons" "$OUT/"
cp "$ROOT"/kindle/{config.xml,menu.json,launch.sh,shortcut_denboku.sh} "$OUT/"
# KUAL shows the version: the tag's (DENBOKU_VERSION), or "dev" between releases.
sed -i "s|<version>.*</version>|<version>${DENBOKU_VERSION:-dev}</version>|" "$OUT/config.xml"
cp "$ROOT/LICENSE" "$ROOT/README.md" "$OUT/"
chmod 755 "$OUT"/bin/* "$OUT"/*.sh
echo "$OUT"
