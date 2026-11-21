#!/usr/bin/env bash
# The self-update of the Linux folder, end to end, in Docker (the ui-test image): a copy of the package
# is served as a later release (signed with the update key), the game installs it by itself, starts again
# and drops the version before; then a release whose program fails at once, which the start script undoes.
#   update-test.sh [package, default dist/denboku-linux.tar.xz] [out dir, default build/update-test]
# AS_ZIP=1 serves the releases as .zip, as the Kindle's are.
# KEY=<secret key> (default ../denboku-shogi-investigation/keys/update-main.key).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PKG=$(realpath "${1:-$ROOT/dist/denboku-linux.tar.xz}") OUT=$(realpath -m "${2:-$ROOT/build/update-test}")
KEY=${KEY:-$ROOT/../denboku-shogi-investigation/keys/update-main.key}
make -C "$ROOT" -s sign-release
SIGN=$ROOT/build/sign-release
rm -rf "$OUT" && mkdir -p "$OUT/srv/good" "$OUT/srv/bad" "$OUT/home"
# The release: the same package under a later build number.
cp "$PKG" "$OUT/srv/good/denboku-linux.tar.xz"
# The broken one: its program exits at once with an error.
mkdir -p "$OUT/bad" && tar xJf "$PKG" -C "$OUT/bad"
printf '#!/bin/sh\nexit 3\n' >"$OUT/bad/denboku-linux/bin/denboku" && chmod 755 "$OUT/bad/denboku-linux/bin/denboku"
tar cJf "$OUT/srv/bad/denboku-linux.tar.xz" -C "$OUT/bad" denboku-linux && rm -rf "$OUT/bad"
F=denboku-linux.tar.xz
if [ "${AS_ZIP:-}" = 1 ]; then
  F=denboku-linux.zip
  for r in good bad; do
    mkdir "$OUT/z" && tar xJf "$OUT/srv/$r/denboku-linux.tar.xz" -C "$OUT/z"
    (cd "$OUT/z" && zip -qr -X "$OUT/srv/$r/$F" denboku-linux) && rm -rf "$OUT/z" "$OUT/srv/$r/denboku-linux.tar.xz"
  done
fi
for r in good bad; do
  read -r size hash < <("$SIGN" hash "$OUT/srv/$r/$F")
  printf '[Release]\nBuild=99999999.%s\n\n[linux-x86_64]\nFile=%s\nSize=%s\nBlake2b=%s\n' \
    "$([ $r = good ] && echo 0001 || echo 0002)" "$F" "$size" "$hash" >"$OUT/srv/$r/update.txt"
  "$SIGN" sign "$KEY" "$OUT/srv/$r/update.txt"
done
printf '[Settings]\nUpdates=1\nTutorialOffered=1\n' >"$OUT/home/settings.ini"
docker image inspect denboku-ui-test:3 >/dev/null 2>&1 || { echo "run tools/ui-test.sh once first (it makes the image)"; exit 1; }
docker run --rm -v "$OUT":/out -v "$PKG":/pkg.tar.xz:ro denboku-ui-test:3 sh -c '
mkdir -p /games && tar xJf /pkg.tar.xz -C /games
Xvfb :9 -screen 0 1000x760x24 >/dev/null 2>&1 &
cd /out/srv && python3 -m http.server 8765 >/out/http.log 2>&1 &
sleep 1
export DISPLAY=:9 DENBOKU_HOME=/out/home LANG=C.UTF-8 DENBOKU_DEBUG=1
G=/games/denboku-linux
started() { pgrep -f "$G/bin/denboku" | head -1; }
DENBOKU_UPDATE_URL=http://127.0.0.1:8765/good/ $G/denboku.sh >/out/app1.log 2>&1 &
first=""; for i in $(seq 20); do first=$(started); [ -n "$first" ] && break; sleep 0.5; done
for i in $(seq 60); do [ -f $G/.update-pending ] && break; sleep 1; done
[ -f $G/.update-pending ] && [ -d $G/old/bin ] && echo "good: installed, old/ kept" || echo "good: NOT installed"
sleep 5; now=$(started)
[ -n "$now" ] && [ "$now" != "$first" ] && echo "good: started again" || echo "good: NOT started again ($first, $now)"
for i in $(seq 45); do [ -d $G/old ] || break; sleep 1; done
[ ! -d $G/old ] && [ ! -f $G/.update-pending ] && echo "good: old/ dropped after the new one ran" || echo "good: old/ still there"
pkill -f "$G/bin/denboku"; sleep 1
cp $G/bin/denboku /tmp/before
DENBOKU_UPDATE_URL=http://127.0.0.1:8765/bad/ $G/denboku.sh >/out/app2.log 2>&1 &
# Installed, run, failed and undone may all happen within a second.
for i in $(seq 60); do [ -f $G/.update-pending ] || [ -f $G/.update-failed ] || grep -q "^UpdateSkip=" /out/home/settings.ini && break; sleep 0.2; done
sleep 8
cmp -s $G/bin/denboku /tmp/before && [ ! -d $G/old ] && echo "bad: the version before is back" || echo "bad: NOT put back"
[ -n "$(started)" ] && echo "bad: the game runs" || echo "bad: the game does not run"
sleep 3; grep -q "^UpdateSkip=99999999.0002" /out/home/settings.ini && echo "bad: that build is skipped from now on" || echo "bad: NOT skipped"
pkill -f "$G/bin/denboku"
grep -c "GET /" /out/http.log | sed "s/^/requests: /"
chown -R '"$(id -u):$(id -g)"' /out' | tee "$OUT/result.txt"
