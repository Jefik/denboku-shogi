#!/bin/sh
# Starts the game in the background and returns at once, so KUAL can close; the game waits
# a moment so it opens after KUAL is gone and stays in front. A running copy may be hidden
# behind the reader: restart it (the game resumes from its autosave). A version just updated
# that fails before it has run a while is replaced by the one before it, kept in old/.
DIR=/mnt/us/extensions/denboku
cd "$DIR" || exit 1
pkill -f "$DIR/bin/denboku" && sleep 1
# Back to old/ only for a game that started with the update untried and failed by itself: a kill
# (this script's pkill, a restart) is not a failure. old/ goes only once all of it is back.
back() {
  [ -n "$pending" ] && [ -d "$DIR/old" ] || return 1
  case $1 in 0|129|130|137|143) return 1 ;; esac
  for f in "$DIR"/old/*; do rm -rf "$DIR/${f##*/}" && mv "$f" "$DIR/" || return 1; done
  rm -rf "$DIR/old"; mv "$DIR/.update-pending" "$DIR/.update-failed" # the game will not take that version again
}
(sleep 1; pending=; [ -f "$DIR/.update-pending" ] && pending=1
 DISPLAY=:0 "$DIR/bin/denboku" >"$DIR/denboku.log" 2>&1; s=$?
 # How each run ended, kept across starts (denboku.log is not): 137 killed (low memory, a restart), 139 a crash.
 { tail -n 19 "$DIR/exits.log" 2>/dev/null; echo "$(date '+%F %T') $s"; } >"$DIR/exits.tmp" && mv "$DIR/exits.tmp" "$DIR/exits.log"
 # Killed or crashed with a field focused, it left the keyboard up over the Kindle's own screens.
 [ $s = 0 ] || lipc-set-prop com.lab126.keyboard close net.denboku 2>/dev/null
 [ $s = 0 ] || { back $s && DISPLAY=:0 exec "$DIR/bin/denboku" >>"$DIR/denboku.log" 2>&1; }) &
exit 0
