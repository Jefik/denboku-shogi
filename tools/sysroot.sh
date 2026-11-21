#!/usr/bin/env bash
# Build a cross-compilation sysroot: shared libraries from the Kindle itself,
# headers and static startup files from Debian wheezy armhf (GTK 2.24 era, the
# closest to the Kindle's GTK 2.20 / GLib 2.29 / glibc 2.20).
#
# Usage: sysroot.sh <dir> [kindle host]   (host defaults to $KINDLE)
# Needs ssh access as root (sshpass with $KINDLE_PASSWORD if set) and curl, dpkg-deb or ar+tar.
set -euo pipefail

DIR=$(realpath -m "$1")
HOST=${2:-${KINDLE:?set KINDLE (the Kindle address) or give it}}
MIRROR=http://archive.debian.org/debian
PACKAGES="libc6-dev linux-libc-dev gcc-4.7 libglib2.0-dev libgtk2.0-dev libcairo2-dev
  libpango1.0-dev libatk1.0-dev libgdk-pixbuf2.0-dev libfreetype6-dev libfontconfig1-dev
  libx11-dev x11proto-core-dev x11proto-xext-dev x11proto-render-dev x11proto-kb-dev
  x11proto-input-dev libxrender-dev libxext-dev libpixman-1-dev libpng12-dev zlib1g-dev
  libexpat1-dev libxcb1-dev libxau-dev libxdmcp-dev libpcre3-dev libxcb-shm0-dev
  libxcb-render0-dev libpthread-stubs0-dev libxinerama-dev libxi-dev libxrandr-dev libxcursor-dev
  libxfixes-dev libxcomposite-dev libxdamage-dev x11proto-xinerama-dev x11proto-randr-dev
  x11proto-fixes-dev x11proto-composite-dev x11proto-damage-dev"

ssh_kindle() {
  if [ -n "${KINDLE_PASSWORD:-}" ]; then sshpass -p "$KINDLE_PASSWORD" ssh "root@$HOST" "$@"
  else ssh "root@$HOST" "$@"; fi
}

mkdir -p "$DIR/debs"
if [ ! -e "$DIR/lib/libc.so.6" ]; then
  echo "Copying libraries from $HOST"
  ssh_kindle 'cd / && tar cf - lib/*.so* usr/lib/*.so* 2>/dev/null' | tar xf - -C "$DIR"
fi

[ -f "$DIR/debs/Packages" ] || curl -sfL "$MIRROR/dists/wheezy/main/binary-armhf/Packages.gz" | gunzip > "$DIR/debs/Packages"
for pkg in $PACKAGES; do
  file=$(awk -v p="$pkg" '$1=="Package:"{m=($2==p)} m&&$1=="Filename:"{print $2; exit}' "$DIR/debs/Packages")
  [ -n "$file" ] || { echo "missing package $pkg" >&2; exit 1; }
  deb="$DIR/debs/$(basename "$file")"
  [ -f "$deb" ] || curl -sfL -o "$deb" "$MIRROR/$file"
  mkdir -p "$DIR/debs/x" && (cd "$DIR/debs/x" && ar x "$deb" && tar xf data.tar.* -C "$DIR/debs/x" && rm -f data.tar.* control.tar.* debian-binary)
done

# Headers and pkg-config files; static pieces glibc needs at link time.
X="$DIR/debs/x"
mkdir -p "$DIR/usr/include" "$DIR/usr/lib/pkgconfig"
cp -an "$X/usr/include/." "$DIR/usr/include/"
find "$X/usr/lib/arm-linux-gnueabihf" -mindepth 1 -maxdepth 1 -type d -exec cp -an {} "$DIR/usr/lib/" \;
for f in crt1.o crti.o crtn.o Scrt1.o libc_nonshared.a libpthread_nonshared.a; do
  cp -n "$X/usr/lib/arm-linux-gnueabihf/$f" "$DIR/usr/lib/"
done
mkdir -p "$DIR/usr/lib/gcc/arm-linux-gnueabihf/4.7"
cp -n "$X"/usr/lib/gcc/arm-linux-gnueabihf/4.7/{crtbegin.o,crtbeginS.o,crtend.o,crtendS.o,libgcc.a} "$DIR/usr/lib/gcc/arm-linux-gnueabihf/4.7/"
find "$X/usr/lib" "$X/usr/share" -name '*.pc' -exec cp -n {} "$DIR/usr/lib/pkgconfig/" \;
# Make the .pc files point at the sysroot layout (libraries in /usr/lib, arch headers in /usr/include).
sed -i 's#/usr/lib/arm-linux-gnueabihf#/usr/include#g; s#^libdir=.*#libdir=/usr/lib#' "$DIR"/usr/lib/pkgconfig/*.pc
echo "sysroot ready: $DIR"
