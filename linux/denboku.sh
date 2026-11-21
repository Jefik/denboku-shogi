#!/bin/sh
# Starts the game with the libraries beside it. A version just updated that fails before it has run a
# while is replaced by the one before it, kept in old/ (src/core/updater.c).
D=$(cd "$(dirname "$0")" && pwd)
run() { GTK2_RC_FILES="$D/gtkrc" LD_LIBRARY_PATH="$D/lib" "$D/bin/denboku" "$@"; }
pending=; [ -f "$D/.update-pending" ] && pending=1 # untried when this run started
run "$@" && exit 0
s=$?
# Not after a kill (logout, a restart): only a game that failed by itself; old/ goes once all of it is back.
case $s in 129|130|137|143) exit $s ;; esac
[ -n "$pending" ] && [ -d "$D/old" ] || exit $s
for f in "$D"/old/*; do rm -rf "$D/${f##*/}" && mv "$f" "$D/" || exit $s; done
rm -rf "$D/old"; mv "$D/.update-pending" "$D/.update-failed" # the game will not take that version again
exec "$D/denboku.sh" "$@"
