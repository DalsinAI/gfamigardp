#!/bin/sh
# GFAmigaRDP: PNG previews of the connection and session windows, laid out
# on the PC by GFOpenLayout UI with the same markup, CSS and theme.
#   OLUI=<gfopenlayout> OGT_ROOT=<opengadtools> [THEME=Open] ./tools/preview/preview.sh [OUT]
# Needs cc and python3 with Pillow. MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT="$HERE/../.."
OLUI=${OLUI:?"OLUI: GFOpenLayout (DalsinAI/gfopenlayout)"}
OGT_ROOT=${OGT_ROOT:?"OGT_ROOT: GFOpenGadTools (amicradle-guest libraries/opengadtools)"}
THEME=${THEME:-Open}
OUT=${1:-"$ROOT/build-preview"}
mkdir -p "$OUT"
python3 - "$OUT/widths.h" <<'PY'
import sys
from PIL import ImageFont
def tab(name):
    try:
        f = ImageFont.truetype(name + ".ttf", 1000)
    except OSError:
        return [600] * 95
    return [round(f.getlength(chr(c))) for c in range(32, 127)]
with open(sys.argv[1], "w") as o:
    for var, name in (("dv_book", "DejaVuSans"), ("dv_bold", "DejaVuSans-Bold")):
        o.write("static const int %s[95] = {%s};\n" % (var, ",".join(map(str, tab(name)))))
PY
make -s -C "$OLUI" OGT_ROOT="$OGT_ROOT" build/libopenlayout.a
cc -O2 -std=c99 -Wall -Wextra -I"$OLUI/include" -I"$OLUI/ui" -I"$OGT_ROOT/lib" -I"$ROOT/src/ui" -I"$OUT" \
  -o "$OUT/preview_ui" "$HERE/preview_ui.c" "$ROOT/src/ui/markup.c" \
  "$OLUI/ui/olui.c" "$OLUI/ui/olui_css.c" "$OLUI/ui/olui_markup.c" "$OLUI/ui/olui_theme.c" \
  "$OGT_ROOT/lib/ogt_theme.c" "$OLUI/build/libopenlayout.a"
T="$OGT_ROOT/themes/$THEME.theme"
# ui_render.py leaves out OL_TEXT_UNDERLINE (4): the keys' underlines drawn here
underline() {
python3 - "$1" "$2" <<'PY'
import sys
from PIL import Image, ImageDraw, ImageFont
img = Image.open(sys.argv[2]).convert("RGBA")
d = ImageDraw.Draw(img)
for line in open(sys.argv[1], encoding="utf-8"):
    f = line.rstrip("\n").split(" ", 6)
    if f[0] != "text" or not int(f[5]) & 4:
        continue
    x, base, size = int(f[1]), int(f[2]), int(f[3])
    v = int(f[4], 16)
    font = ImageFont.truetype("DejaVuSans-Bold.ttf" if int(f[5]) & 1 else "DejaVuSans.ttf", size)
    w = font.getlength(f[6])
    d.line([(x, base + 2), (x + w - 1, base + 2)], fill=((v >> 16) & 255, (v >> 8) & 255, v & 255, 255))
img.save(sys.argv[2])
PY
}
"$OUT/preview_ui" connect "$T" "$OUT/connect.ops"
python3 "$OLUI/tests/ui_render.py" "$OUT/connect.ops" "$OUT/connect-$THEME.png"
underline "$OUT/connect.ops" "$OUT/connect-$THEME.png"
TB=$("$OUT/preview_ui" session "$T" "$OUT/session.ops" "$OUT/desktop.ppm" | sed 's/toolbar \([0-9]*\).*/\1/')
python3 "$OLUI/tests/ui_render.py" "$OUT/session.ops" "$OUT/session-ui.png"
underline "$OUT/session.ops" "$OUT/session-ui.png"
python3 - "$OUT/session-ui.png" "$OUT/desktop.ppm" "$TB" "$OUT/session-$THEME.png" <<'PY'
import sys
from PIL import Image
ui = Image.open(sys.argv[1]).convert("RGB")
ui.paste(Image.open(sys.argv[2]).convert("RGB"), (0, int(sys.argv[3])))
ui.save(sys.argv[4])
PY
ls -l "$OUT"/*.png
