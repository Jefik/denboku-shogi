#!/usr/bin/env bash
# Build the Android APKs into <out>/Denboku.apk (arm64, phones) and <out>/Denboku-x86_64.apk (the
# emulator, Chromebooks), each with its architecture only (default out: dist):
#   build-android.sh [out]
# The game runs as on Linux, on an X server inside the app: the Termux packages of GTK 2 and Xvfb
# (tools/android-sysroot.py), laid out by tools/android-stage.py, and a Java shell (android/).
# Needs the SDK/NDK and Java 17 (docs/BUILD.md); ABIS picks the Termux architectures.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(realpath -m "${1:-$ROOT/dist}")
INV=${INV:-$ROOT/../denboku-shogi-investigation}
ABIS=${ABIS:-aarch64 x86_64}
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-17-openjdk}
export ANDROID_HOME=${ANDROID_HOME:-$HOME/Android/Sdk}
NDK=${ANDROID_NDK:-$ANDROID_HOME/ndk/27.2.12479018}
CLANG=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang
B=$ROOT/build/android
mkdir -p "$B/assets"
rm -rf "$B/jniLibs"
for arch in $ABIS; do
  case $arch in
    aarch64) abi=arm64-v8a target=aarch64-linux-android24 ;;
    arm) abi=armeabi-v7a target=armv7a-linux-androideabi24 ;;
    x86_64) abi=x86_64 target=x86_64-linux-android24 ;;
  esac
  S=$(realpath -m "$INV/android-sysroot/$arch")
  [ -f "$S/packages.txt" ] || python3 "$ROOT/tools/android-sysroot.py" "$arch" "$INV/android-sysroot"
  [ -f "$B/engine/$abi/libfairystockfish.so" ] || "$ROOT/tools/build-engine-android.sh" "$B/engine"
  [ -f "$B/curl/$abi/curl" ] || "$ROOT/tools/build-curl-android.sh" "$B/curl"
  "$CLANG" --target=$target -O2 -shared -fPIC -s -Wl,--version-script="$ROOT/android/app/src/main/c/crypto.map" \
    -Wl,-soname,libsha1.so -o "$B/crypto-$abi.so" "$ROOT/android/app/src/main/c/crypto.c"
  make -C "$ROOT" -s -j"$(nproc)" android SYSROOT="$S" ANDROID_ARCH="$arch" ANDROID_NDK="$NDK"
  python3 "$ROOT/tools/android-stage.py" "$S" "$ROOT/build/android-$arch/denboku" "$B/jniLibs/$abi" "$B/assets/runtime.zip" "$CLANG" "$target" "$B/curl/$abi/curl" "$B/crypto-$abi.so"
  "$CLANG" --target=$target -O2 -shared -fPIC -s -o "$B/jniLibs/$abi/libscreen.so" "$ROOT/android/app/src/main/c/screen.c" -landroid
  "$CLANG" --target=$target -O2 -shared -fPIC -s -o "$B/jniLibs/$abi/libfbshim.so" "$ROOT/android/app/src/main/c/fbshim.c" -ldl
done
cd "$ROOT/android"
# Packaged from scratch: Gradle's incremental packaging leaves the old runtime.zip's bytes inside the APK.
rm -rf app/build/outputs/apk app/build/intermediates/apk
./gradlew -q :app:assembleDebug --daemon
mkdir -p "$OUT"
rm -f "$OUT/Denboku.apk" "$OUT/Denboku-x86_64.apk"
A=app/build/outputs/apk/debug
[ -f $A/app-arm64-v8a-debug.apk ] && cp $A/app-arm64-v8a-debug.apk "$OUT/Denboku.apk"
[ -f $A/app-x86_64-debug.apk ] && cp $A/app-x86_64-debug.apk "$OUT/Denboku-x86_64.apk"
ls "$OUT"/Denboku*.apk | sed 's/^/android: /'
