#!/bin/sh
# gfopenntlm for AmigaOS 3.x with the os32 GCC 16 stove (bebbo's amiga-gcc,
# GCC 16, libnix).
#   STOVE  the stove (default ~/AmiCradle/stoves/os32-gcc16)
#   CPU    040 (default: the 68040, no FPU needed) or 060
#   OUT    the output directory (default build-amiga)
#   HOST_TESTS=0  skip the PC tests (tests/run_host_tests.sh) when a host
#          C compiler is there
# Outputs: lib/gfopenntlm.library, lib/libgfopenntlm.a (the static link),
# include/ (libraries, clib, inline, proto, fd, sfd), docs/gfopenntlm.doc,
# tests/GFOpenNTLMLibTest (the library), tests/GFOpenNTLMStaticTest (the
# static link), tests/GFOpenNTLMVectors (all the PC vectors, on the Amiga).
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SPECS="$HERE/specs/m68k-soft-m68040.specs"
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16"}
P="$STOVE/prefix"
CC=${CC_AMIGA:-"$P/bin/m68k-amigaos-gcc"}
AR=${AR_AMIGA:-"$P/bin/m68k-amigaos-ar"}
OUT=${OUT:-"$HERE/build-amiga"}
case "${CPU:-040}" in
    040) CPUFLAGS="-m68040 -specs=$SPECS" ;;
    060) CPUFLAGS="-m68060" ;;
    *) echo "CPU is 040 or 060"; exit 2 ;;
esac

# the generated headers are committed; check them when Python is here
if command -v python3 >/dev/null 2>&1; then
    python3 "$HERE/tools/gen_headers.py" --check
fi

if [ "${HOST_TESTS:-1}" != 0 ] && command -v "${HOST_CC:-cc}" >/dev/null 2>&1; then
    OUT="$OUT/host" sh "$HERE/tests/run_host_tests.sh"
fi

CORE="gn_md4 gn_md5 gn_sha256 gn_hmac gn_rc4 gn_util gn_der gn_ntlm gn_cssp gn_api gn_sys_amiga"
mkdir -p "$OUT/lib" "$OUT/obj" "$OUT/tests" "$OUT/docs" "$OUT/include"
cp -R "$HERE/include/." "$OUT/include/"
cp "$HERE/docs/gfopenntlm.doc" "$OUT/docs/"

# -fno-tree-loop-distribute-patterns and -fno-delete-null-pointer-checks:
# as gfopentls (the stove's GCC 16 and address 0 being memory on an Amiga)
CFLAGS="$CPUFLAGS -O2 -fno-tree-loop-distribute-patterns -fno-delete-null-pointer-checks -fomit-frame-pointer -Wall -Wextra -Werror -Wno-unused-parameter -I$HERE/include -I$HERE/src"

OBJS=
for f in $CORE; do
    "$CC" $CFLAGS -noixemul -c "$HERE/src/$f.c" -o "$OUT/obj/$f.o"
    OBJS="$OBJS $OUT/obj/$f.o"
done
rm -f "$OUT/lib/libgfopenntlm.a"
"$AR" rcs "$OUT/lib/libgfopenntlm.a" $OBJS

SRC=
for f in $CORE; do SRC="$SRC $HERE/src/$f.c"; done
"$CC" $CFLAGS -mcrt=nix20 -fno-toplevel-reorder -nostartfiles \
  -o "$OUT/lib/gfopenntlm.library" "$HERE/library/gn_lib.c" $SRC \
  -lamiga -lgcc -Wl,-Map,"$OUT/lib/gfopenntlm.library.map"
echo "$OUT/lib/gfopenntlm.library ($(wc -c < "$OUT/lib/gfopenntlm.library") bytes)"
"$P/bin/m68k-amigaos-nm" -u "$OUT/lib/gfopenntlm.library"

"$CC" $CFLAGS -noixemul -o "$OUT/tests/GFOpenNTLMLibTest" "$HERE/tests/test_library.c"
"$CC" $CFLAGS -noixemul -DGFOPENNTLM_STATIC -o "$OUT/tests/GFOpenNTLMStaticTest" \
  "$HERE/tests/test_library.c" "$OUT/lib/libgfopenntlm.a"
"$CC" $CFLAGS -noixemul -o "$OUT/tests/GFOpenNTLMVectors" "$HERE/tests/test_vectors.c" \
  "$OUT/lib/libgfopenntlm.a"
for f in GFOpenNTLMLibTest GFOpenNTLMStaticTest GFOpenNTLMVectors; do
    echo "$OUT/tests/$f ($(wc -c < "$OUT/tests/$f") bytes)"
done
(cd "$OUT" && sha256sum lib/gfopenntlm.library lib/libgfopenntlm.a tests/* > SHA256SUMS)
cat "$OUT/SHA256SUMS"
