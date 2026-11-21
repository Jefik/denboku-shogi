#!/usr/bin/env bash
# The README's screenshots, from the Linux bundle in a virtual X server (ui-test.sh), as WebP:
#   readme-screenshots.sh [denboku-linux dir]   (dist/denboku-linux by default)
# desktop.webp: a game with the engine's hint; looks-1.webp and looks-2.webp: two shots each, every one a
# different palette, piece set and variant; kindle-game.webp, kindle-menu.webp and kindle-lesson.webp: the
# Kindle's look on the computer (--kindle-look, a Paperwhite's screen), shown side by side.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
APP=$(realpath "${1:-$ROOT/dist/denboku-linux}")
OUT=$ROOT/docs/screenshots
WORK=$(mktemp -d)
trap 'docker run --rm -v "$WORK":/w debian:bookworm sh -c "rm -rf /w/*" >/dev/null 2>&1; rm -rf "$WORK"' EXIT
# A middle game with pieces in hand on both sides.
GAME="ln1g3nl/1r1sk1gb1/p1pppp1pp/1p4s2/7p1/2P3P2/PPSPPP2P/1BK2G1R1/LN1G2SNL b Pp 1"

# shot <name> <WxH> <dpi> <sfen or ""> <settings...> [-- steps...]: the window alone, as <name>.png.
shot() {
  local name=$1 size=$2 dpi=$3 sfen=$4; shift 4
  local settings=() steps=()
  while [ $# -gt 0 ] && [ "$1" != -- ]; do settings+=("$1"); shift; done
  [ "${1:-}" = -- ] && shift
  steps=("$@")
  printf '[Settings]\nLanguage=en\n%s\n' "$(printf '%s\n' "${settings[@]}")" >"$WORK/$name.ini"
  SIZE=$size SCREEN=${size%x*}x$((${size#*x} + 60))x24 DPI=$dpi DENBOKU_SFEN=$sfen \
    "$ROOT/tools/ui-test.sh" "$APP" "$WORK/$name" "$WORK/$name.ini" -- "sleep 5" "${steps[@]}" "shot s" >/dev/null 2>&1
  magick "$WORK/$name/s.png" -crop "$size+0+0" +repage "$WORK/$name.png"
}
pair() { # out, two shots
  magick "$WORK/$2.png" "$WORK/$3.png" -bordercolor '#20242c' -border 6 +append -quality 90 "$OUT/$1"
}

rm -f "$OUT"/*.svg
echo "desktop..."
shot desktop 1440x900 115 "$GAME" PaletteName=Slate Pieces=printout-kanji1.ini -- "key h" "sleep 6"
magick "$WORK/desktop.png" -quality 90 "$OUT/desktop.webp"

echo "looks..."
P=900x1240
shot wood $P 144 "$GAME" PaletteName=Wood WoodBoard=1 Pieces=kanji2-antique.ini
shot mini $P 144 "" PaletteName=Navy Pieces=printout-western.ini ColoredPieces=1 Variant=1
shot dobutsu $P 144 "" PaletteName=Sakura Variant=4
shot tori $P 144 "" PaletteName=Green\ felt Variant=6
pair looks-1.webp wood mini
pair looks-2.webp dobutsu tori

echo "kindle..."
# A Paperwhite's screen (1236×1648 at 298 dpi), in English; taps on Menu, Help, Tutorial and the knight's lesson.
printf '[Settings]\nLanguage=en\n' >"$WORK/kindle.ini"
kindle() { # name, steps...
  local name=$1; shift
  DENBOKU_SFEN=$GAME DENBOKU_KINDLE_LOOK=1 SCREEN=1236x1648x24 DPI=298 "$ROOT/tools/ui-test.sh" "$APP" "$WORK/$name" \
    "$WORK/kindle.ini" -- "sleep 5" "$@" "shot s" >/dev/null 2>&1
  magick "$WORK/$name/s.png" -colorspace Gray -resize 50% "$WORK/$name.png"
}
kindle k-game
kindle k-menu "click 117 1593" "sleep 2"
kindle k-lesson "click 117 1593" "sleep 2" "click 767 1585" "sleep 2" "click 168 1208" "sleep 3" "click 420 1110" "sleep 3"
for k in game menu lesson; do
  magick "$WORK/k-$k.png" -bordercolor '#20242c' -border 4 -quality 90 "$OUT/kindle-$k.webp"
done
ls -la "$OUT"
