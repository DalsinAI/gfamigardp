#!/bin/sh
# GFAmigaRDP for AmigaOS 3.2 on the 68040, with the os32 GCC 16 stove
# (amiga-gcc, GCC 16, libnix) and OpenNDK's headers.
#   STOVE     the stove (default ~/AmiCradle/stoves/os32-gcc16)
#   CPU       040 (default: soft float, any 68040 or 68LC040) or 060
#   OUT       the output directory (default build-amiga)
#   WORK      objects and the Open SDK parts (default .build, beside this)
#   DEPS      where the Open SDK parts go (default $WORK/deps): gfopensdk,
#             gfopentls, gfopenlayout, amicradle-guest and gfopenntlm at
#             pinned commits, fetched by tools/fetch_deps.sh, or the
#             checkouts named in GFOPENSDK=, GFOPENTLS=, OLUI=, OGT_GUEST=
#             and GFOPENNTLM=
#   NTLM      shared (default: gfopenntlm.library, opened at run time) or
#             static (libgfopenntlm.a linked in)
# Outputs: GFAmigaRDP (the client: "GFAmigaRDP DEMO" and "GFAmigaRDP DEMO
# SESSION" need no server), tools/RDPProbe, tests/X224Test, Themes/*.theme,
# Env/GFOpenGadTools/Look, README-lab.txt, Libs/gfopentls.library and
# Libs/gfopencrypto.library (from the pinned gfopentls; LIBS=0 skips them),
# Libs/gfopenntlm.library (NLA's NTLM and CredSSP), tests/GFOpenNTLM*,
# tools/GFOpenTLSClient; lib/libolui.a for reference.
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16"}
P="$STOVE/prefix"
CC=${CC:-"$P/bin/m68k-amigaos-gcc"}
OUT=${OUT:-"$HERE/build-amiga"}
WORK=${WORK:-"$HERE/.build"}
DEPS=${DEPS:-"$WORK/deps"}
sh "$HERE/tools/fetch_deps.sh" "$DEPS"
OPENNDK="$DEPS/gfopensdk/OpenNDK"
SPECS="$DEPS/gfopensdk/Lib/m68k-soft-m68040.specs"
GFOPENTLS="$DEPS/gfopentls"
OLUI="$DEPS/gfopenlayout"
OGT_ROOT="$DEPS/amicradle-guest/libraries/opengadtools"

for d in "$OPENNDK/Include_H" "$GFOPENTLS/include" "$OLUI/include" "$OGT_ROOT/lib" "$OGT_ROOT/themes"; do
    [ -d "$d" ] || { echo "build-amiga.sh: no $d"; exit 2; }
done
[ -f "$SPECS" ] || { echo "build-amiga.sh: no $SPECS"; exit 2; }
[ -x "$CC" ] || { echo "build-amiga.sh: no compiler $CC (STOVE=)"; exit 2; }
"$CC" --version | head -1
case "${CPU:-040}" in
    040) CPUFLAGS="-m68040 -specs=$SPECS" ;;
    060) CPUFLAGS="-m68060" ;;
    *) echo "CPU is 040 or 060"; exit 2 ;;
esac

# libolui.a built here from the pinned gfopenlayout, with this stove
LIBOLUI="$WORK/olui/libolui.a"
if [ ! -f "$LIBOLUI" ] || [ "$(cat "$WORK/olui/.pin" 2>/dev/null)" != "$(cat "$OLUI/.pin" 2>/dev/null)" ]; then
    echo "building libolui.a"
    OGT_ROOT="$OGT_ROOT" SPECS="$SPECS" STOVE="$STOVE" CC="$CC" AR="${AR:-${CC%gcc}ar}" OUT="$WORK/olui" sh "$OLUI/build-ui-amiga.sh"
    cat "$OLUI/.pin" > "$WORK/olui/.pin" 2>/dev/null || :
fi

# gfopenntlm (NTLM and CredSSP for NLA, DalsinAI/gfopenntlm at its pin),
# built by its own script with this
# stove: the library, libgfopenntlm.a and the headers GFAmigaRDP uses
GFOPENNTLM="$DEPS/gfopenntlm"
GN="$WORK/gfopenntlm"
echo "building gfopenntlm"
STOVE="$STOVE" CC_AMIGA="$CC" AR_AMIGA="${AR:-${CC%gcc}ar}" CPU="${CPU:-040}" OUT="$GN" \
    sh "$GFOPENNTLM/build-amiga.sh" > "$WORK/gfopenntlm.log" 2>&1 || { tail -30 "$WORK/gfopenntlm.log"; exit 1; }
grep -E "^(passed|failed|PASS|FAIL)|passed, " "$WORK/gfopenntlm.log" || :
case "${NTLM:-shared}" in
    shared) GNFLAGS="" GNLIB="" ;;
    static) GNFLAGS="-DGFOPENNTLM_STATIC" GNLIB="$GN/lib/libgfopenntlm.a" ;;
    *) echo "NTLM is shared or static"; exit 2 ;;
esac

# OpenNDK's headers first, then the compiler's and libnix's own
GCCINC=$("$CC" -print-file-name=include)
INC="-nostdinc -isystem $GCCINC -isystem $OPENNDK/Include_H"
for d in "$P/m68k-amigaos/libnix/include" "$P/m68k-amigaos/sys-include" "$P/m68k-amigaos/include" "$P/include"; do
    [ -d "$d" ] && INC="$INC -isystem $d"
done
# -fno-delete-null-pointer-checks: address 0 is memory on an Amiga (GCC
# would put a TRAP where it proves a pointer null);
# -fno-late-combine-instructions: as libolui.a is built (GCC 16.2.0b)
CFLAGS="$CPUFLAGS $INC -D__AMIGA__ -DAMIGAOS3 -noixemul -std=gnu99 -O2 \
  -fno-delete-null-pointer-checks -fno-late-combine-instructions -Wall -Wextra \
  -I$HERE/src/amiga -I$HERE/src/crypto -I$HERE/src/core -I$HERE/src/codec -I$HERE/src/ui \
  -I$GFOPENTLS/include -I$OLUI/include -I$GN/include $GNFLAGS"

mkdir -p "$WORK/obj" "$OUT/tools" "$OUT/tests" "$OUT/lib" "$OUT/Themes" "$OUT/Env/GFOpenGadTools"
LOG="$OUT/warnings.log"
: > "$LOG"

OBJS=""
for f in src/amiga/amipr_init.c src/amiga/amipr_time.c src/amiga/amipr_atomic.c src/amiga/amipr_wait.c \
         src/amiga/amipr_dos.c src/amiga/amipr_net.c src/amiga/amipr_crypto.c src/amiga/amipr_tls.c src/amiga/amipr_clip.c src/amiga/conntask.c \
         src/crypto/x509.c src/core/x224.c src/core/transport.c src/core/connect.c src/core/nla.c src/core/mcs.c src/core/utf16.c src/core/pdu.c src/core/input.c src/core/session.c src/core/arc.c src/core/knownhosts.c src/core/vchan.c src/core/drdynvc.c src/core/cliprdr.c src/core/klid.c src/core/rdpgfx.c src/codec/gfx_fb.c src/codec/zgfx.c src/codec/planar.c src/codec/progressive.c src/codec/clearcodec.c; do
    o="$WORK/obj/$(basename "$f" .c).o"
    "$CC" $CFLAGS -c "$HERE/$f" -o "$o" 2>>"$LOG"
    OBJS="$OBJS $o"
done
UIOBJS=""
for f in src/ui/main.c src/ui/connect_win.c src/ui/session_win.c src/ui/desktop.c \
         src/ui/favourites.c src/ui/markup.c; do
    o="$WORK/obj/ui_$(basename "$f" .c).o"
    "$CC" $CFLAGS -c "$HERE/$f" -o "$o" 2>>"$LOG"
    UIOBJS="$UIOBJS $o"
done

"$CC" $CFLAGS -s -o "$OUT/GFAmigaRDP" $UIOBJS $OBJS "$LIBOLUI" $GNLIB -lamiga 2>>"$LOG"
"$CC" $CFLAGS -s -o "$OUT/tools/RDPProbe" "$HERE/tests/rdpprobe.c" $OBJS $GNLIB -lamiga 2>>"$LOG"
"$CC" $CFLAGS -s -o "$OUT/tests/X224Test" "$HERE/tests/x224_test.c" "$WORK/obj/x224.o" 2>>"$LOG"
"$CC" $CFLAGS -s -o "$OUT/tests/NLAOffline" "$HERE/tests/nla_offline.c" $GNLIB -lamiga 2>>"$LOG"
"$CC" $CFLAGS -s -o "$OUT/tests/NLAStages" "$HERE/tests/nla_stages.c" $GNLIB -lamiga 2>>"$LOG"
# the same two with gfopenntlm linked in (libgfopenntlm.a), whatever NTLM= is
STFLAGS=$(printf '%s' "$CFLAGS" | sed 's/ -DGFOPENNTLM_STATIC//')
"$CC" $STFLAGS -DGFOPENNTLM_STATIC -s -o "$OUT/tests/NLAOfflineStatic" "$HERE/tests/nla_offline.c" "$GN/lib/libgfopenntlm.a" -lamiga 2>>"$LOG"
"$CC" $STFLAGS -DGFOPENNTLM_STATIC -s -o "$OUT/tests/NLAStagesStatic" "$HERE/tests/nla_stages.c" "$GN/lib/libgfopenntlm.a" -lamiga 2>>"$LOG"
cp "$LIBOLUI" "$OUT/lib/libolui.a"
mkdir -p "$OUT/Libs"
cp "$GN/lib/gfopenntlm.library" "$OUT/Libs/"
cp "$GN/tests/GFOpenNTLMLibTest" "$GN/tests/GFOpenNTLMStaticTest" "$GN/tests/GFOpenNTLMVectors" "$OUT/tests/"

# the GFOpenGadTools theme GFAmigaRDP's windows are drawn in: ENV:GFOpenGadTools/Look
# names SYS:Prefs/Presets/Themes/<Look>.theme ("Open" when unset)
cp "$OGT_ROOT"/themes/*.theme "$OUT/Themes/"
printf 'Open\n' > "$OUT/Env/GFOpenGadTools/Look"
cp "$HERE/docs/LAB.txt" "$OUT/README-lab.txt"

# the libraries GFAmigaRDP opens at run time, built by the pinned gfopentls's
# own scripts (unchanged) with this stove: gfopencrypto.library (which
# gfopentls.library opens) and gfopentls.library, for SYS:Libs/.
# LIBS=0 leaves them out.
if [ "${LIBS:-1}" != 0 ]; then
    mkdir -p "$OUT/Libs"
    for s in build-amiga.sh build-amiga-tls.sh; do
        echo "building the libraries: gfopentls/$s"
        STOVE="$STOVE" CC="$CC" CPU="${CPU:-040}" OUT="$WORK/libs" sh "$GFOPENTLS/$s" > "$WORK/libs-$s.log" 2>&1 ||
            { tail -20 "$WORK/libs-$s.log"; exit 1; }
    done
    cp "$WORK/libs/lib/gfopencrypto.library" "$WORK/libs/lib/gfopentls.library" "$OUT/Libs/"
    cp "$WORK/libs/tests/GFOpenTLSClient" "$OUT/tools/"
    printf '%s\n' "$(cat "$GFOPENTLS/.pin" 2>/dev/null)" > "$OUT/Libs/GFOPENTLS-PIN"
fi

W=$(grep -c "warning:" "$LOG" || true)
for f in GFAmigaRDP tools/RDPProbe tests/X224Test tests/NLAOffline tests/NLAOfflineStatic tests/NLAStages tests/NLAStagesStatic Libs/gfopenntlm.library; do
    echo "$OUT/$f ($(wc -c < "$OUT/$f") bytes)"
done
echo "themes: $(ls "$OUT/Themes" 2>/dev/null | tr '\n' ' ')"
echo "warnings: $W (warnings.log)"
(cd "$OUT" && sha256sum GFAmigaRDP tools/RDPProbe tests/X224Test tests/NLAOffline tests/NLAOfflineStatic tests/NLAStages tests/NLAStagesStatic tests/GFOpenNTLM* $(ls Libs/*.library 2>/dev/null) > SHA256SUMS && cat SHA256SUMS)
