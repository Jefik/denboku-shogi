#!/usr/bin/env bash
# Rebuild every font in data/fonts/ from its source in ../denboku-shogi-investigation: make-fonts.sh
# Subsets (tools/font-subset.sh), traced sets (tools/build_trace_font.py), the UI font
# (tools/make-ui-font.py) and the animals' colors (tools/make-animal-colors.py). Needs fonttools,
# Pillow, numpy, rsvg-convert, ImageMagick and potrace. Licenses are in data/fonts/ and the README.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
INV=${INV:-$(cd "$ROOT/../denboku-shogi-investigation" && pwd)}
F=$INV/fonts-candidates PO=$INV/piece-sets/shogi_printout
X=$PO/extracted/xboard FC=$PO/fonts/fonts.conf
OUT=$ROOT/data/fonts WORK=$(mktemp -d)
trap 'rm -r "$WORK"' EXIT
cd "$ROOT"

# Piece kanji, cut to the ~30 characters the sets use (Tori shogi's birds too).
tools/font-subset.sh "$F/noto-medium/NotoSansJP-Medium.otf" "$F/ZenAntique-Regular.ttf"
# The 墨 behind the move list, in the art's brush font.
tools/font-subset.sh -o "$WORK" -t 墨 "$F/YujiSyuku-Regular.ttf"
mv "$WORK/YujiSyuku-Regular-shogi.ttf" "$OUT/YujiSyuku-sumi.ttf"
# The animals and Tori shogi's birds (data/styles/dobutsu.ini), then the colors under their outlines.
# The phoenix is a ligature (bird, ZWJ, fire) the game does not shape: its glyph gets U+E000, then the subset.
python3 - "$F/noto-emoji/NotoEmoji-SemiBold.ttf" "$WORK/NotoEmoji-SemiBold.ttf" <<'PY'
import sys
from fontTools.ttLib import TTFont
f = TTFont(sys.argv[1])
for t in f["cmap"].tables:
    if t.isUnicode():
        t.cmap[0xE000] = "phoenix"
f.save(sys.argv[2])
PY
tools/font-subset.sh -o "$WORK" -t 🦁🦒🐘🐕🐈🐇🐗🐥🐉🐎🐅🦘🦏🐔$'\ue000'🦩🐓🐦🕊🪿🦜🦅 "$WORK/NotoEmoji-SemiBold.ttf"
mv "$WORK/NotoEmoji-SemiBold-shogi.ttf" "$OUT/NotoEmoji-shogi.ttf"
python3 tools/make-animal-colors.py >/dev/null

# The UI font.
python3 tools/make-ui-font.py "$INV/ui-fonts/NotoSansJP[wght].ttf" "$INV/ui-fonts/NotoSansSymbols2-Regular.ttf"

# Traced sets: lishogi's International Monochrome 2D (pictorial) and peanatsu's shogi_printout.
trace() { python3 tools/build_trace_font.py "$@" >/dev/null; }
# The kings are its "international" set's crown, by the same author, instead of a stylized 王.
L=$ROOT/../lishogi/ui/@build/pieces/assets/standard
cp -r "$L/intl_monochrome_2d" "$WORK/intl-svg"
for k in 0OU 0GY 1OU 1GY; do cp "$L/international/$k.svg" "$WORK/intl-svg/"; done
trace "$WORK/intl-svg" "$WORK/intl" "$OUT/ShogiIntl.otf" --name "Shogi Intl"
trace "$PO/extracted/svg-source/uniform_western" "$WORK/western" "$OUT/ShogiWestern.otf" --printout 1 --name ShogiWestern \
  --copyright "Western symbols from peanatsu's shogi_printout, CC BY-SA 3.0, with chess pieces by Colin M.L. Burnett and NikNaks, a lance by Orror/Zigeuner and promoted pieces by Hari Seldon (Wikimedia Commons)"
# Tori shogi's kanji in AR PL UKai's own brush (UnGungseo lacks some and Gata Sōsho draws some blank: those
# sets are not offered there).
UKAI=$F/arphic-ukai/ARPLUKaiCN.ttf
trace "$X/kanji" "$WORK/k1" "$OUT/ShogiPrintoutKanji.otf" --xboard 1 --fontconfig "$FC" --name ShogiPrintoutKanji --stroke 0.015 \
  --extra "U+E000=$X/2kanji/WhiteGoldPawn.svg" \
  --copyright "Traced from peanatsu's shogi_printout (CC BY-SA 3.0); glyphs of Un fonts UnGungseo and UnBatang (c) Koaunghi Un, GPL-2"
trace "$X/2kanji" "$WORK/k2" "$OUT/ShogiPrintoutKanji2.otf" --xboard 1 --fontconfig "$FC" --name ShogiPrintoutKanji2 \
  --codes FU,KY,KE,GI,KI,KA,HI,OU,GY --grow 4 --tori "$UKAI" \
  --copyright "Traced from peanatsu's shogi_printout (CC BY-SA 3.0); glyphs of AR PL UKai CN (c) 1999 Arphic Technology, Arphic Public License; modified 2026-11-09: traced to outlines, strokes made bolder"
trace "$X/2kanji" "$WORK/cursive" "$OUT/ShogiCursive.otf" --xboard 1 --fontconfig "$FC" --name ShogiCursive \
  --codes NY,NK,NG,UM,RY \
  --copyright "Promoted pieces by Hari Seldon (Wikimedia Commons) in peanatsu's shogi_printout, CC BY-SA 3.0"
# Tori shogi's birds for the pictorial set, drawn by tools/make-birds.py in its style.
python3 tools/make-birds.py "$WORK/birds-svg"
trace "$WORK/birds-svg" "$WORK/birds" "$OUT/ShogiBirds.otf" --birds 1 --name ShogiBirds \
  --copyright "Tori shogi's birds, drawn for Denboku Shōgi (tools/make-birds.py)"
# Gata Sōsho, a thin cursive brush: set and traced bolder, so it reads on a small koma and on e-ink.
trace - "$WORK/gata" "$OUT/ShogiGata.otf" --font "$F/gatasosyo/gatasosyo.ttf" --name ShogiGata --grow 12 \
  --copyright "Traced from gatasosyo (Gata Sosho) by gata, free to use, modify and redistribute; strokes made bolder"
ls -l "$OUT"
