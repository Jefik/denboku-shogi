#!/usr/bin/env bash
# The whole game under ASan and UBSan, driven through its screens by ui-test.sh (a move, the
# engine's answer, a hint, the moves, Preferences, the menu's pages, undo, the close button):
#   asan-test.sh [fairy-stockfish x86_64 binary] [out dir, default build/asan]
# Fails on a sanitizer report or when the game does not quit at the end; the log is <out>/ui/app.log.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
ENGINE=${1:-$ROOT/../denboku-shogi-investigation/fsf-build-shogi/fairy-stockfish-x86_64} OUT=$(realpath -m "${2:-$ROOT/build/asan}")
SANITIZE=1 "$ROOT/tools/build-linux.sh" "$ENGINE" "$OUT" >"$OUT.log" 2>&1 || { cat "$OUT.log"; exit 1; }
rm -rf "$OUT/ui" "$OUT.log"
# In a 1100x760 window: the pawn on 7g to 7f.
STEPS=("sleep 4" "click 194 551" "click 194 470" "sleep 8" "shot move" "key h" "sleep 5"
  "key Page_Up" "key Page_Up" "key Home" "key End" "key f" "key f" "key ctrl+z" "sleep 1" "key ctrl+y" "sleep 1"
  "key ctrl+comma" "sleep 1" "key Down" "key Return" "sleep 1")
for _ in 1 2 3 4 5 6 7 8; do STEPS+=("key ctrl+Next" "sleep 1"); done
STEPS+=("shot preferences" "key F10" "sleep 1")
# The menu's pages: New Game, Games, Help.
for keys in "" "Right Right" "Right Right Down Down"; do
  STEPS+=("key F10" "sleep 1")
  for k in $keys; do STEPS+=("key $k"); done
  STEPS+=("key Return" "sleep 2" "key F10" "sleep 1")
done
STEPS+=("shot end" "close")
# Leaks are not looked for: the only ones are fontconfig's and GTK's own.
RESULT=$(ASAN_OPTIONS=detect_leaks=0:abort_on_error=0 UBSAN_OPTIONS=print_stacktrace=1 SIZE=1100x760 \
  "$ROOT/tools/ui-test.sh" "$OUT/denboku-linux" "$OUT/ui" -- "${STEPS[@]}")
if grep -a -E "runtime error|AddressSanitizer" "$OUT/ui/app.log"; then
  grep -a -A30 -m1 -E "runtime error|AddressSanitizer" "$OUT/ui/app.log"
  exit 1
fi
grep -q "close: the game quit" <<<"$RESULT" || { echo "the game did not quit at the end"; exit 1; }
echo "asan: no reports"
