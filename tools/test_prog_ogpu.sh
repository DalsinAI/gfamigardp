#!/bin/sh
# Progressive through OpenGPU's v1.10 ops against the CPU path (host).
#   GFOPENRTG=../gfopenrtg tools/test_prog_ogpu.sh [seeds]
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
R=${GFOPENRTG:-"$HERE/../gfopenrtg"}
[ -f "$R/library/opengpu/ogpu_dwt.c" ] || { echo "GFOPENRTG= a gfopenrtg checkout with stream v1.10"; exit 2; }
OUT=${OUT:-/tmp/gfamigardp-host}
mkdir -p "$OUT"
cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -I"$HERE/src/codec" -I"$R/include" -I"$R/library/opengpu" -o "$OUT/test_prog_ogpu" \
    "$HERE/tests/host/test_prog_ogpu.c" "$HERE/src/codec/progressive.c" "$HERE/src/codec/gfx_fb.c" \
    "$R/library/opengpu/ogpu_core.c" "$R/library/opengpu/ogpu_dwt.c" "$R/library/opengpu/ogpu_build.c"
"$OUT/test_prog_ogpu" "${1:-40}"
