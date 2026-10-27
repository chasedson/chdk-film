#!/bin/sh
# Build the host preview tool. film.c is copied next to the stub headers so its
# quoted #includes resolve to the stand-ins here rather than the real CHDK headers.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/build}
mkdir -p "$out/src"
cp "$here"/*.h "$out/src/"
cp "$here/../../core/film.c" "$here/../../core/film.h" "$here/preview.c" "$out/src/"
sed -i '' 's|"../../core/film.h"|"film.h"|' "$out/src/preview.c"
cc -O2 -Wall -Wextra -I"$out/src" -o "$out/preview" "$out/src/preview.c" "$out/src/film.c"
echo "$out/preview"
