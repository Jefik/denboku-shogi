#!/usr/bin/env bash
# Usage: build.sh [platform...] [options]
# Runs the tests and builds the platforms named (Linux when none is): linux (the folder, tarball and
# AppImage), kindle (the KUAL package), windows (the portable zip), android (the APKs), flatpak, arm (the
# ARM64 tarball, cross-built), or all (every one but flatpak and arm, with the checks); then installs on
# the phone if adb reaches it (Wi-Fi or USB) and on the Kindle if its SSH answers. Inputs come from
# ../denboku-shogi-investigation (sysroots and engines); outputs go to dist/. Prints each step and its time,
# then which platforms were updated and which were not (../denboku-shogi-investigation/builds.txt keeps
# the latest build of each platform and what each device has).
#   --checks           also the fuzzer and the game under ASan (tools/asan-test.sh, about 2.5 min)
#   --release          every platform, flatpak and arm too, with the checks
#   --no-restart       don't restart the game on the Kindle after installing
#   --dev              start the game on the Kindle and the phone in developer mode (DENBOKU_DEV: a
#                      profile of its own with the default settings, the player's left alone), for
#                      that start only; the next one from the device is a normal one
#   -h, --help         show this help
# Environment: INV (the inputs folder, ../denboku-shogi-investigation by default), KINDLE (address),
# KINDLE_PASSWORD, PHONE (address), ANDROID_HOME; $INV/devices.env may set them.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
INV=${INV:-$ROOT/../denboku-shogi-investigation}
if [ -f "$INV/devices.env" ]; then set -a; . "$INV/devices.env"; set +a; fi
KINDLE=${KINDLE:-} PASSWORD=${KINDLE_PASSWORD:-kindle} PHONE=${PHONE:-}
export INV
KINDLE_ON=0 LINUX=0 WINDOWS=0 ANDROID=0 FLATPAK=0 ARM=0 RESTART=1 DEV=0 APK= CHECKS=0 NAMED=0
for a in "$@"; do
  NAMED=$((NAMED + 1))
  case $a in
    linux) LINUX=1 ;; kindle) KINDLE_ON=1 ;; windows) WINDOWS=1 ;; android) ANDROID=1 ;;
    flatpak) FLATPAK=1 ;; arm) ARM=1 ;;
    all) KINDLE_ON=1 LINUX=1 WINDOWS=1 ANDROID=1 CHECKS=1 ;;
    --release) KINDLE_ON=1 LINUX=1 WINDOWS=1 ANDROID=1 FLATPAK=1 ARM=1 CHECKS=1 ;;
    --checks) CHECKS=1; NAMED=$((NAMED - 1)) ;;
    --no-restart) RESTART=0; NAMED=$((NAMED - 1)) ;;
    --dev) DEV=1; NAMED=$((NAMED - 1)) ;;
    -h|--help) sed -n '2,/^[^#]/{/^#/s/^# \{0,1\}//p}' "$0"; exit 0 ;;
    *) echo "unknown platform or option $a (see --help)"; exit 2 ;;
  esac
done
[ $NAMED -gt 0 ] || LINUX=1
# These need the bundle from the previous step.
[ $FLATPAK = 1 ] && LINUX=1
[ $CHECKS = 1 ] && LINUX=1
cd "$ROOT"
LOG=$(mktemp)
# builds.txt: one line per platform and per device, the latest first written over.
BUILDS=$INV/builds.txt UPDATED=
record() { # record <name> <text>
  touch "$BUILDS"
  { grep -v "^$1:" "$BUILDS" | grep -v "^#" || true; echo "$1: $2"; } | sort >"$BUILDS.new"
  { echo "# Latest build of each platform and what each device has (tools/build.sh writes it)"; cat "$BUILDS.new"; } >"$BUILDS"
  rm "$BUILDS.new"
  UPDATED="$UPDATED $1"
}
summary() {
  echo
  echo "updated:${UPDATED:- nothing}"
  local rest
  rest=$(grep -v "^#" "$BUILDS" 2>/dev/null | while IFS=: read -r name text; do
    case " $UPDATED " in *" $name "*) ;; *) echo "  $name:$text" ;; esac; done)
  [ -n "$rest" ] && { echo "not updated (latest they have):"; echo "$rest"; }
  true
}
trap summary EXIT
step() { # step <name> <command...>: runs quietly, shows the log on failure, and how long it took
  local name=$1 t=$SECONDS; shift
  if "$@" >"$LOG" 2>&1; then
    if grep -qE "warning:|error:" "$LOG"; then echo "$name: ok, with warnings ($((SECONDS - t)) s)"; grep -E "warning:|error:" "$LOG" | sort -u | head -20
    else echo "$name: ok ($((SECONDS - t)) s)"; fi
  else echo "$name: FAILED"; tail -40 "$LOG"; exit 1; fi
}
# The version from the commit's tag (v0.9.0), empty between releases.
export DENBOKU_VERSION=${DENBOKU_VERSION-$(git describe --tags --exact-match --match 'v[0-9]*' 2>/dev/null | sed 's/^v//' || true)}
# One build number (About) for every platform, UTC: the tagged commit's time, which the Windows build on
# GitHub (.github/workflows/windows.yml) gets too, else now. The binaries built here are made again for it.
export DENBOKU_BUILD=${DENBOKU_BUILD:-$(if [ -n "$DENBOKU_VERSION" ]; then TZ=UTC git log -1 --format=%cd --date=format-local:%Y%m%d.%H%M; else date -u +%Y%m%d.%H%M; fi)}
rm -f build/kindle/denboku build/android-*/denboku
echo "build $DENBOKU_BUILD${DENBOKU_VERSION:+, version $DENBOKU_VERSION}"
# Each package's build beside it (dist/<file>.build), which tools/release.sh checks.
stamp() { for f; do [ -f "dist/$f" ] && echo "$DENBOKU_BUILD" >"dist/$f.build"; done; return 0; }
NOW=$(date "+%Y-%m-%d %H:%M")
J=-j$(nproc)
step test make $J test
[ $CHECKS = 0 ] || step fuzz make $J fuzz
if [ $KINDLE_ON = 1 ]; then
  step kindle make $J kindle SYSROOT="$INV/sysroot"
  step package tools/package-kindle.sh
  rm -f dist/denboku-kindle.zip dist/denboku-kindle.tar.gz*
  (cd dist && zip -qr -X denboku-kindle.zip denboku)
  stamp denboku-kindle.zip
  record kindle-package "build $DENBOKU_BUILD, $NOW"
fi
if [ $LINUX = 1 ]; then
  # The AppImage tools are outside the repo; without them build-linux.sh would only make the
  # folder and tarball, so say so instead of leaving dist/ with an old AppImage.
  if [ ! -x "$INV/appimage/appimagetool-x86_64.AppImage" ] || [ ! -f "$INV/appimage/runtime-x86_64" ]; then
    echo "linux: missing $INV/appimage (appimagetool-x86_64.AppImage and runtime-x86_64)"
    exit 1
  fi
  step linux tools/build-linux.sh
  [ -f dist/Denboku-x86_64.AppImage ] || { echo "linux: AppImage not built"; exit 1; }
  grep -E "warning:|error:" dist/build.log | head -20 || true
  [ $CHECKS = 0 ] || step asan tools/asan-test.sh
  stamp denboku-linux.tar.xz Denboku-x86_64.AppImage
  record linux "build $DENBOKU_BUILD, $NOW (dist/denboku-linux, AppImage)"
fi
if [ $WINDOWS = 1 ]; then
  step windows tools/build-windows.sh
  grep -E "warning:|error:" dist/build-windows.log | head -20 || true
  stamp denboku-windows.zip
  record windows "build $DENBOKU_BUILD, $NOW (dist, zip)"
fi
if [ $ANDROID = 1 ]; then
  if [ -n "${ANDROID_HOME:-}" ] || [ -d "$HOME/Android/Sdk" ]; then
    step android tools/build-android.sh dist
    APK=dist/Denboku.apk
    stamp Denboku.apk Denboku-x86_64.apk
    record android-apk "build $DENBOKU_BUILD, $NOW"
  else
    echo "android: SDK/NDK not found, skipped (see docs/ANDROID.md)"
  fi
fi
if [ $FLATPAK = 1 ]; then
  if command -v flatpak >/dev/null 2>&1; then
    step flatpak tools/build-flatpak.sh dist/denboku-linux dist
    record flatpak "build $DENBOKU_BUILD, $NOW"
  else
    echo "flatpak: flatpak not found, skipped"
  fi
fi
if [ $ARM = 1 ]; then
  step arm env ARCH=arm64 tools/build-linux.sh
  stamp denboku-linux-arm64.tar.xz
  record arm64 "build $DENBOKU_BUILD, $NOW"
fi
rm -f "$LOG"

# The phone, over Wi-Fi debugging (its port changes: mDNS tells it) or USB; its game stays.
if [ -n "$APK" ] && command -v adb >/dev/null 2>&1; then
  TARGET=
  [ -z "$PHONE" ] || TARGET=$(adb mdns services 2>/dev/null | grep -o "$PHONE:[0-9]*" | head -1 || true)
  [ -z "$TARGET" ] || timeout 10 adb connect "$TARGET" >/dev/null 2>&1 || true
  DEVICE=$([ -z "$PHONE" ] || adb devices 2>/dev/null | awk -v p="$PHONE:" '$2 == "device" && index($1, p) == 1 { print $1; exit }')
  # Or plugged in: the first device that is not an emulator.
  [ -n "$DEVICE" ] || DEVICE=$(adb devices 2>/dev/null | awk '$2 == "device" && $1 !~ /^emulator-/ { print $1; exit }')
  # An APK signed with another key (the debug one, before the release key) is uninstalled first.
  install_apk() { OUT=$(timeout 180 adb -s "$DEVICE" install -r "$APK" 2>&1) && return 0
    case $OUT in *INSTALL_FAILED_UPDATE_INCOMPATIBLE*) adb -s "$DEVICE" uninstall jp.denboku >/dev/null 2>&1
      echo "phone: signed with another key, uninstalled first"; timeout 180 adb -s "$DEVICE" install -r "$APK" >/dev/null 2>&1 ;; *) return 1 ;; esac; }
  if [ -n "$DEVICE" ] && install_apk; then
    echo "phone: installed"
    record phone "build $DENBOKU_BUILD, installed $(date "+%Y-%m-%d %H:%M")"
    if [ $DEV = 1 ]; then
      adb -s "$DEVICE" shell "run-as jp.denboku touch files/dev" && adb -s "$DEVICE" shell am force-stop jp.denboku &&
        adb -s "$DEVICE" shell am start -n jp.denboku/.MainActivity >/dev/null && echo "phone: started in developer mode"
    fi
  else echo "phone: not reachable, not installed"; fi
fi

[ $KINDLE_ON = 1 ] || exit 0
[ -n "$KINDLE" ] || { echo "kindle: set KINDLE (its address) to install it"; exit 0; }
ssh_k() { sshpass -p "$PASSWORD" ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no "root@$KINDLE" "$@" 2>/dev/null; }
if ! ssh_k true; then echo "kindle: not reachable, not installed"; exit 0; fi
DIR=/mnt/us/extensions/denboku
if ! ssh_k "touch /mnt/us/.denboku-write && rm /mnt/us/.denboku-write"; then
  echo "kindle: its storage is read-only (check it with dosfsck), not installed"; exit 0
fi
# Earlier versions were called kindle-shogi: move that install, keeping its settings and autosave.
ssh_k "[ -d /mnt/us/extensions/kindle-shogi ] && [ ! -d $DIR ] && mv /mnt/us/extensions/kindle-shogi $DIR; \
  rm -f $DIR/bin/kindle-shogi $DIR/shortcut_shogi.sh $DIR/kindle-shogi.log" || true
# Everything but the binaries is replaced in place (the data folders whole, so files the game no
# longer has go too); the running binary is swapped by rename.
ssh_k "cd $DIR 2>/dev/null && rm -rf art fonts sounds styles problems lessons" || true
tar -C dist/denboku --exclude=./bin -cf - . | ssh_k "mkdir -p $DIR/bin && tar -C $DIR -xf -"
sshpass -p "$PASSWORD" scp -q dist/denboku/bin/denboku "root@$KINDLE:$DIR/bin/denboku.new" 2>/dev/null
ssh_k "cd $DIR && mv bin/denboku.new bin/denboku && chmod 755 bin/denboku *.sh"
# The engine too, when it changed (a running one keeps its old file through the rename).
if [ "$(md5sum < dist/denboku/bin/fairy-stockfish)" != "$(ssh_k "md5sum < $DIR/bin/fairy-stockfish")" ]; then
  sshpass -p "$PASSWORD" scp -q dist/denboku/bin/fairy-stockfish "root@$KINDLE:$DIR/bin/fairy-stockfish.new" 2>/dev/null
  ssh_k "cd $DIR && mv bin/fairy-stockfish.new bin/fairy-stockfish && chmod 755 bin/fairy-stockfish"
fi
# A library shortcut, old or current, is replaced with the current one.
D=/mnt/us/documents
ssh_k "if [ -f $D/shortcut_shogi.sh ]; then rm -rf $D/shortcut_shogi.sh $D/shortcut_shogi.sh.sdr; \
  cp $DIR/shortcut_denboku.sh $D/; elif [ -f $D/shortcut_denboku.sh ]; then cp $DIR/shortcut_denboku.sh $D/; fi" || true
RUNNING=0
ssh_k "pidof denboku >/dev/null || pidof kindle-shogi >/dev/null" && RUNNING=1
if [ $DEV = 1 ]; then
  ssh_k "kill \$(pidof denboku) \$(pidof kindle-shogi) 2>/dev/null; sleep 1; cd $DIR && (DENBOKU_DEV=1 DISPLAY=:0 ./bin/denboku >denboku.log 2>&1 &)"
  echo "kindle: installed and started in developer mode"
elif [ $RESTART = 1 ] && [ $RUNNING = 1 ]; then
  # The game resumes from its autosave.
  ssh_k "kill \$(pidof denboku) \$(pidof kindle-shogi) 2>/dev/null; sleep 1; cd $DIR && (DISPLAY=:0 ./bin/denboku >denboku.log 2>&1 &)"
  echo "kindle: installed and restarted"
else
  echo "kindle: installed"
fi
record kindle "build $DENBOKU_BUILD, installed $(date "+%Y-%m-%d %H:%M")"
