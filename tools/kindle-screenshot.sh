#!/usr/bin/env bash
# Grab the Kindle screen over SSH: kindle-screenshot.sh <out.png> [host]
# 8-bit gray framebuffer; size and stride follow the current rotation.
set -euo pipefail
HOST=${2:-${KINDLE:?set KINDLE (the Kindle address) or give it}}
ssh_kindle() {
  if [ -n "${KINDLE_PASSWORD:-}" ]; then sshpass -p "$KINDLE_PASSWORD" ssh "root@$HOST" "$@"
  else ssh "root@$HOST" "$@"; fi
}
read -r stride w h < <(ssh_kindle 'echo $(cat /sys/class/graphics/fb0/stride) $(DISPLAY=:0 xwininfo -root | awk "/Width/{w=\$2} /Height/{h=\$2} END{print w, h}")')
ssh_kindle "dd if=/dev/fb0 bs=$stride count=$h 2>/dev/null" |
  magick -size "${stride}x${h}" -depth 8 gray:- -crop "${w}x${h}+0+0" +repage "$1"
