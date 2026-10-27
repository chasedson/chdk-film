#!/bin/sh
# Build the host tools. film.c is copied next to the stub headers so its quoted
# #includes resolve to the stand-ins here rather than the real CHDK headers.
#   preview     - film looks on a simulated A570 raw frame
#   ui_preview  - picker / badge / developing card rendered at screen size
set -e
here=$(cd "$(dirname "$0")" && pwd)
root="$here/../.."
out=${1:-$here/build}
mkdir -p "$out/src"
cp "$here"/*.h "$here"/*.c "$out/src/"
cp "$root/core/film.c" "$root/core/film.h" "$root/lib/font/font_8x16_uni.h" "$out/src/"
sed -i '' 's|"../../core/film.h"|"film.h"|' "$out/src/preview.c"
cd "$out/src"
cc -O2 -Wall -Wextra -DFILM_HOST_PREVIEW -I. -o "$out/preview" preview.c film.c host_draw.c
cc -O2 -Wall -Wextra -DFILM_HOST_PREVIEW -I. -o "$out/ui_preview" ui_preview.c film.c host_draw.c
echo "$out"
