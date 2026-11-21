#!/usr/bin/env bash
# Build curl for the Android ABIs, static and on mbed TLS, in place of Termux's curl, whose libcurl,
# OpenSSL, nghttp2/3, ngtcp2 and libssh2 weighed some 7 MB in each APK: build-curl-android.sh <out dir>
# Writes <out>/<abi>/curl. HTTP and HTTPS only, the CA bundle at Termux's path (android-stage.py moves
# it to the app's). The sources are the submodules third_party/curl and third_party/mbedtls.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(realpath -m "$1")
NDK=${ANDROID_NDK:-${ANDROID_HOME:-$HOME/Android/Sdk}/ndk/27.2.12479018}
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
build() { # build <abi>
  local T=(-DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI="$1" -DANDROID_PLATFORM=24
           -DCMAKE_BUILD_TYPE=MinSizeRel -G Ninja
           -DCMAKE_C_FLAGS="-ffile-prefix-map=$ROOT/=  -ffile-prefix-map=$W/=") # no local paths in the binary
  cmake -S "$ROOT/third_party/mbedtls" -B "$W/$1/mbedtls" "${T[@]}" -DCMAKE_INSTALL_PREFIX="$W/$1/prefix" \
    -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DUSE_SHARED_MBEDTLS_LIBRARY=OFF >"$W/$1.log" 2>&1 || { tail -20 "$W/$1.log"; exit 1; }
  cmake --build "$W/$1/mbedtls" --target install >>"$W/$1.log" 2>&1 || { tail "$W/$1.log"; exit 1; }
  cmake -S "$ROOT/third_party/curl" -B "$W/$1/curl" "${T[@]}" -DMBEDTLS_INCLUDE_DIR="$W/$1/prefix/include" \
    -DMBEDTLS_LIBRARY="$W/$1/prefix/lib/libmbedtls.a" -DMBEDX509_LIBRARY="$W/$1/prefix/lib/libmbedx509.a" \
    -DMBEDCRYPTO_LIBRARY="$W/$1/prefix/lib/libmbedcrypto.a" \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_CURL_EXE=ON -DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF \
    -DENABLE_CURL_MANUAL=OFF -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF -DHTTP_ONLY=ON \
    -DCURL_USE_MBEDTLS=ON -DCURL_USE_OPENSSL=OFF -DCURL_ZLIB=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DUSE_NGHTTP2=OFF \
    -DUSE_LIBIDN2=OFF -DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF -DCURL_DISABLE_ALTSVC=ON -DCURL_DISABLE_HSTS=ON \
    -DCURL_DISABLE_COOKIES=ON -DCURL_DISABLE_NETRC=ON -DCURL_DISABLE_PROXY=OFF -DENABLE_UNIX_SOCKETS=OFF \
    -DCURL_CA_BUNDLE=/data/data/com.termux/files/usr/etc/tls/cert.pem -DCURL_CA_PATH=none >>"$W/$1.log" 2>&1 || { tail -20 "$W/$1.log"; exit 1; }
  cmake --build "$W/$1/curl" --target curl >>"$W/$1.log" 2>&1 || { tail "$W/$1.log"; exit 1; }
  mkdir -p "$OUT/$1"
  "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" "$W/$1/curl/src/curl" -o "$OUT/$1/curl"
}
for abi in arm64-v8a armeabi-v7a x86_64; do build "$abi"; done
ls -la "$OUT"/*/curl
