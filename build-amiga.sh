#!/bin/sh
# GFAmigaRDP for AmigaOS 3.2 on the 68040, with the os32 GCC 16 stove
# (amiga-gcc, GCC 16, libnix) and OpenNDK's headers.
#   STOVE     the stove (default ~/AmiCradle/stoves/os32-gcc16)
#   CPU       040 (default: soft float, any 68040 or 68LC040) or 060
#   OUT       the output directory (default build-amiga)
#   OPENNDK   OpenNDK (default ../gfopensdk/OpenNDK); its Include_H is used
#   SPECS     the soft-float specs (default $OPENNDK/../Lib/m68k-soft-m68040.specs)
#   GFOPENTLS gfopentls, for <gfopentls/...> (default ../gfopentls)
#   OLUI      gfopenlayout, for libolui.a (default ../gfopenlayout)
#   OGT_ROOT  GFOpenGadTools (amicradle-guest libraries/opengadtools): its
#             themes go into $OUT, and libolui.a is built with it if missing
# Outputs: GFAmigaRDP (the client: "GFAmigaRDP DEMO" and "GFAmigaRDP DEMO
# SESSION" need no server), tools/RDPProbe, tests/X224Test, Themes/*.theme,
# Env/GFOpenGadTools/Look, README-lab.txt; lib/libolui.a for reference.
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16"}
P="$STOVE/prefix"
CC=${CC:-"$P/bin/m68k-amigaos-gcc"}
OUT=${OUT:-"$HERE/build-amiga"}
OPENNDK=${OPENNDK:-"$HERE/../gfopensdk/OpenNDK"}
SPECS=${SPECS:-"$OPENNDK/../Lib/m68k-soft-m68040.specs"}
GFOPENTLS=${GFOPENTLS:-"$HERE/../gfopentls"}
OLUI=${OLUI:-"$HERE/../gfopenlayout"}
OGT_ROOT=${OGT_ROOT:-}

for d in "$OPENNDK/Include_H" "$GFOPENTLS/include" "$OLUI/include"; do
    [ -d "$d" ] || { echo "build-amiga.sh: no $d (see the variables at the top)"; exit 2; }
done
[ -x "$CC" ] || { echo "build-amiga.sh: no compiler $CC (STOVE=)"; exit 2; }
case "${CPU:-040}" in
    040) CPUFLAGS="-m68040 -specs=$SPECS" ;;
    060) CPUFLAGS="-m68060" ;;
    *) echo "CPU is 040 or 060"; exit 2 ;;
esac

LIBOLUI="$OLUI/build-amiga/ui/libolui.a"
if [ ! -f "$LIBOLUI" ]; then
    [ -n "$OGT_ROOT" ] || { echo "build-amiga.sh: $LIBOLUI is missing: give OGT_ROOT= to build it"; exit 2; }
    echo "building libolui.a in $OLUI"
    OGT_ROOT="$OGT_ROOT" SPECS="$SPECS" STOVE="$STOVE" "$OLUI/build-ui-amiga.sh"
fi

# OpenNDK's headers first, then the compiler's and libnix's own
GCCINC=$("$CC" -print-file-name=include)
INC="-nostdinc -isystem $GCCINC -isystem $OPENNDK/Include_H -isystem $P/m68k-amigaos/libnix/include \
  -isystem $P/m68k-amigaos/sys-include -isystem $P/m68k-amigaos/include -isystem $P/include"
# -fno-delete-null-pointer-checks: address 0 is memory on an Amiga (GCC
# would put a TRAP where it proves a pointer null);
# -fno-late-combine-instructions: as libolui.a is built (GCC 16.2.0b)
CFLAGS="$CPUFLAGS $INC -D__AMIGA__ -DAMIGAOS3 -noixemul -std=gnu99 -O2 \
  -fno-delete-null-pointer-checks -fno-late-combine-instructions -Wall -Wextra \
  -I$HERE/src/amiga -I$HERE/src/crypto -I$HERE/src/core -I$HERE/src/ui \
  -I$GFOPENTLS/include -I$OLUI/include"

mkdir -p "$OUT/obj" "$OUT/tools" "$OUT/tests" "$OUT/lib" "$OUT/Themes" "$OUT/Env/GFOpenGadTools"
LOG="$OUT/obj/warnings.log"
: > "$LOG"

OBJS=""
for f in src/amiga/amipr_init.c src/amiga/amipr_time.c src/amiga/amipr_atomic.c src/amiga/amipr_wait.c \
         src/amiga/amipr_dos.c src/amiga/amipr_net.c src/amiga/amipr_crypto.c src/amiga/amipr_tls.c \
         src/crypto/x509.c src/core/x224.c src/core/transport.c src/core/connect.c; do
    o="$OUT/obj/$(basename "$f" .c).o"
    "$CC" $CFLAGS -c "$HERE/$f" -o "$o" 2>>"$LOG"
    OBJS="$OBJS $o"
done
UIOBJS=""
for f in src/ui/main.c src/ui/connect_win.c src/ui/session_win.c src/ui/desktop.c \
         src/ui/favourites.c src/ui/markup.c; do
    o="$OUT/obj/ui_$(basename "$f" .c).o"
    "$CC" $CFLAGS -c "$HERE/$f" -o "$o" 2>>"$LOG"
    UIOBJS="$UIOBJS $o"
done

"$CC" $CFLAGS -s -o "$OUT/GFAmigaRDP" $UIOBJS $OBJS "$LIBOLUI" -lamiga 2>>"$LOG"
"$CC" $CFLAGS -s -o "$OUT/tools/RDPProbe" "$HERE/tests/rdpprobe.c" $OBJS -lamiga 2>>"$LOG"
"$CC" $CFLAGS -s -o "$OUT/tests/X224Test" "$HERE/tests/x224_test.c" "$OUT/obj/x224.o" 2>>"$LOG"
cp "$LIBOLUI" "$OUT/lib/libolui.a"

# the GFOpenGadTools theme GFAmigaRDP's windows are drawn in: ENV:GFOpenGadTools/Look
# names SYS:Prefs/Presets/Themes/<Look>.theme ("Open" when unset)
if [ -n "$OGT_ROOT" ] && [ -d "$OGT_ROOT/themes" ]; then
    cp "$OGT_ROOT"/themes/*.theme "$OUT/Themes/"
fi
printf 'Open\n' > "$OUT/Env/GFOpenGadTools/Look"
cp "$HERE/docs/LAB.txt" "$OUT/README-lab.txt"

W=$(grep -c "warning:" "$LOG" || true)
for f in GFAmigaRDP tools/RDPProbe tests/X224Test; do
    echo "$OUT/$f ($(wc -c < "$OUT/$f") bytes)"
done
echo "themes: $(ls "$OUT/Themes" 2>/dev/null | tr '\n' ' ')"
echo "warnings: $W (obj/warnings.log)"
(cd "$OUT" && sha256sum GFAmigaRDP tools/RDPProbe tests/X224Test > SHA256SUMS && cat SHA256SUMS)
