#!/usr/bin/env bash
# Subset fonts to the shogi piece characters, keeping TTF/OTF so the Kindle's
# FreeType can read them (it predates WOFF2).
# Fork of cjk-font-subset.sh. Requires fonttools.
#
# Usage: font-subset.sh [-o <dir>] [-t <text>] <font>...
set -euo pipefail

OUTPUT=data/fonts
TEXT="歩兵香車桂馬銀将金角行飛王玉と杏圭全成龍燕鴈鷹鵰鶴雉鶉鵬左右"

while getopts "o:t:" opt; do
  case $opt in
    o) OUTPUT=$OPTARG ;;
    t) TEXT=$OPTARG ;;
    *) sed -n '2,7p' "$0"; exit 1 ;;
  esac
done
shift $((OPTIND - 1))
[ $# -gt 0 ] || { sed -n '2,7p' "$0"; exit 1; }

mkdir -p "$OUTPUT"
for font in "$@"; do
  file=$(basename "$font")
  target="$OUTPUT/${file%.*}-shogi.${file##*.}"
  python3 -m fontTools.subset "$font" --text="$TEXT" --layout-features='*' \
    --ignore-missing-glyphs --desubroutinize --output-file="$target"
  echo "$file: $(du -h "$font" | cut -f1) -> $(du -h "$target" | cut -f1)"
done
