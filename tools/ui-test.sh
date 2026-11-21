#!/usr/bin/env bash
# Drive the Linux bundle in a virtual X server and take screenshots:
#   ui-test.sh <denboku-linux dir> <out dir> [settings.ini] -- <step>...
# A step is "click X Y", "move X Y" (hover), "rdrag X1 Y1 X2 Y2" (right-button drag), "hold X1 Y1 X2 Y2" (press,
# hold, drag), "flick X1 Y1 X2 Y2" (a quick drag let go while moving), "type TEXT" (one word), "key NAME", "sleep S", "close" (the window's close button, again for the question; says whether the game quit) or "shot NAME". Runs in Docker, so no GTK is needed on the host.
# "follow" then takes more steps from <out dir>/steps.in as lines are added to it, until "end" (to act on
# what the game shows, as with a phone on the other side). DOCKER_ARGS adds options to docker run
# (--network host: the game on the computer's own network, for LAN games with other devices; two such runs at
# once need XDISP=<n> on one, as they share the X display's socket).
# SIZE=WxH sets the window's size; SCREEN=WxHxDEPTH and DPI the virtual screen's (1280x900x24, 96); a Kindle
# Paperwhite's, with DENBOKU_KINDLE_LOOK=1 (its look), is SCREEN=1236x1648x8 DPI=298. UPDATES=<n> the update setting (3, off, by default; 0 asks the first question),
# DENBOKU_UPDATE_URL a test server for them (with DENBOKU_DEBUG). DENBOKU_SVG=/out/<file>[:seconds] writes the view as an SVG there and quits (denboku --help). AUTOSAVE=<kif> starts with that game (with a [Game] group in the settings). PRE=<command> runs in the
# container, in the background, before the game (a fake network peer kept in the out dir, as /out); DENBOKU_NET=join:127.0.0.1 has the game join it, DENBOKU_NET=host:<password> host a room with that password.
set -euo pipefail
APP=$(realpath "$1") OUT=$(realpath -m "$2") SETTINGS=
shift 2
[ "${1:-}" = "--" ] || { SETTINGS=${1:-}; shift || true; }
[ "${1:-}" = "--" ] && shift
mkdir -p "$OUT/home"
[ -n "$SETTINGS" ] && cp "$SETTINGS" "$OUT/home/settings.ini"
# SIZE=WxH: the window's size (with no window manager it cannot be resized once open).
if [ -n "${SIZE:-}" ]; then
  [ -f "$OUT/home/settings.ini" ] || echo "[Settings]" >"$OUT/home/settings.ini"
  sed -i -e '/^Window\(Width\|Height\)=/d' -e "s/^\[Settings\]\$/[Settings]\nWindowWidth=${SIZE%x*}\nWindowHeight=${SIZE#*x}/" "$OUT/home/settings.ini"
fi
# UPDATES=<0-3>: the update setting (default 3, off: the first question would cover the steps); 0 asks it.
[ -f "$OUT/home/settings.ini" ] || echo "[Settings]" >"$OUT/home/settings.ini"
grep -q '^\[Settings\]' "$OUT/home/settings.ini" || printf '\n[Settings]\n' >>"$OUT/home/settings.ini"
grep -q '^Updates=' "$OUT/home/settings.ini" || sed -i "s/^\[Settings\]\$/[Settings]\nUpdates=${UPDATES:-3}/" "$OUT/home/settings.ini"
# OFFER=1: the tutorial's offer at the first start (by default taken as made: it opens, after the updates question, as soon as the board shows and takes the first keys).
[ "${OFFER:-0}" = 1 ] || grep -q '^TutorialOffered=' "$OUT/home/settings.ini" || sed -i "s/^\[Settings\]\$/[Settings]\nTutorialOffered=1/" "$OUT/home/settings.ini"
[ -n "${AUTOSAVE:-}" ] && cp "$AUTOSAVE" "$OUT/home/autosave.kif"
STEPS=$(printf '%s\n' "$@")
# Build tools live in a cached image, so builds need no network.
docker image inspect denboku-ui-test:3 >/dev/null 2>&1 ||
  printf 'FROM debian:bookworm\nRUN apt-get update -qq && apt-get install -y -qq xvfb xdotool imagemagick fonts-noto-cjk curl ca-certificates x11-utils python3 procps xz-utils && rm -rf /var/lib/apt/lists/*\n' |
  docker build -q -t denboku-ui-test:3 - >/dev/null
# The window's close button, as a window manager presses it (Xvfb has none).
CLOSE='import ctypes, sys
x = ctypes.CDLL("libX11.so.6")
x.XOpenDisplay.restype = ctypes.c_void_p
x.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
x.XInternAtom.restype = ctypes.c_ulong
class Event(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int), ("serial", ctypes.c_ulong), ("send", ctypes.c_int), ("display", ctypes.c_void_p),
                ("window", ctypes.c_ulong), ("message", ctypes.c_ulong), ("format", ctypes.c_int), ("l", ctypes.c_long * 5)]
d, w = x.XOpenDisplay(None), int(sys.argv[1])
e = Event(33, 0, 1, d, w, x.XInternAtom(d, b"WM_PROTOCOLS", 0), 32)
e.l[0] = x.XInternAtom(d, b"WM_DELETE_WINDOW", 0)
x.XSendEvent.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_long, ctypes.c_void_p]
x.XSendEvent(d, w, 0, 0, ctypes.byref(e))
x.XFlush.argtypes = [ctypes.c_void_p]
x.XFlush(d)'
docker run --rm ${DOCKER_ARGS:-} -e XDISP="${XDISP:-9}" -e CLOSE="$CLOSE" -v "$APP":/app:ro -v "$OUT":/out -e STEPS="$STEPS" -e DENBOKU_SFEN="${DENBOKU_SFEN:-}" -e DENBOKU_SVG="${DENBOKU_SVG:-}" -e DENBOKU_KINDLE_LOOK="${DENBOKU_KINDLE_LOOK:-}" -e SCREEN="${SCREEN:-1280x900x24}" -e DPI="${DPI:-96}" -e DENBOKU_DEBUG="${DENBOKU_DEBUG:-}" -e DENBOKU_NET="${DENBOKU_NET:-}" -e DENBOKU_UPDATE_URL="${DENBOKU_UPDATE_URL:-}" -e ASAN_OPTIONS -e UBSAN_OPTIONS -e PRE="${PRE:-}" denboku-ui-test:3 sh -c '
set -e
Xvfb :$XDISP -screen 0 $SCREEN -dpi $DPI >/dev/null 2>&1 &
sleep 1
export DISPLAY=:$XDISP DENBOKU_HOME=/out/home LANG=C.UTF-8
[ -n "$DENBOKU_SFEN" ] || unset DENBOKU_SFEN
[ -n "$DENBOKU_SVG" ] || unset DENBOKU_SVG
[ -n "$DENBOKU_KINDLE_LOOK" ] || unset DENBOKU_KINDLE_LOOK
[ -n "$DENBOKU_DEBUG" ] || unset DENBOKU_DEBUG
[ -n "$DENBOKU_NET" ] || unset DENBOKU_NET
[ -n "$DENBOKU_UPDATE_URL" ] || unset DENBOKU_UPDATE_URL
[ -z "$PRE" ] || sh -c "$PRE" &
/app/denboku.sh >/out/app.log 2>&1 &
sleep 3
step() {
  cmd=$1 a=$2 b=$3
  case "$cmd" in
    click) xdotool mousemove "$a" "$b" click 1 ;;
    move) xdotool mousemove "$a" "$b" ;;
    type) xdotool type "$a" ;;
    key) xdotool key "$a" ;;
    rdrag) set -- $a $b; xdotool mousemove "$1" "$2" mousedown 3 mousemove "$3" "$4" mouseup 3 ;;
    hold) set -- $a $b; xdotool mousemove "$1" "$2" mousedown 1 sleep 0.8 mousemove "$3" "$4" mouseup 1 ;;
    flick) set -- $a $b; xdotool mousemove "$1" "$2" mousedown 1
      for i in 1 2 3 4 5 6 7 8; do xdotool mousemove $(($1 + ($3 - $1) * i / 8)) $(($2 + ($4 - $2) * i / 8)) sleep 0.012; done
      xdotool mouseup 1 ;;
    sleep) sleep "$a" ;;
    shot) import -window root "/out/$a.png" ;;
    close) for w in $(xdotool search --name Denboku); do python3 -c "$CLOSE" "$w"; done; sleep 1
      pidof denboku >/dev/null && { for w in $(xdotool search --name Denboku); do python3 -c "$CLOSE" "$w"; done; sleep 1; } # Quit? asks first
      pidof denboku >/dev/null && echo "close: still running" || echo "close: the game quit" ;;
    follow) : >/out/steps.in; chmod 666 /out/steps.in
      tail -f /out/steps.in | while read -r c x y; do [ "$c" = end ] && break; step "$c" "$x" "$y"; done
      pkill tail || true ;;
  esac
}
echo "$STEPS" | while read -r cmd a b; do step "$cmd" "$a" "$b"; done
chown -R '"$(id -u):$(id -g)"' /out'
ls "$OUT"
