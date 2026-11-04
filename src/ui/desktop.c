/*
 * GFAmigaRDP: the session window's desktop area. Until the RDP output is
 * written (docs/DESIGN.md 6) it shows a test pattern at the remote size:
 * a gradient, a grid, two mock windows, a grey ramp and colour bars.
 *
 * Drawing, best first (DESIGN.md 8): on a true-colour screen 24-bit rows
 * through cybergraphics.library's WritePixelArray (RECTFMT_RGB); on a
 * palette screen a 3x3x3 colour cube of pens (ObtainBestPen) with a 4x4
 * ordered dither, through graphics.library's WriteChunkyPixels (V40).
 * The gfopengpu stream comes in front of both once the session draws.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "ui.h"

#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/view.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <inline/macros.h>
#include <stdio.h>
#include <string.h>

/* cybergraphics.library by its vectors (interface facts from its
 * autodoc): WritePixelArray -126, GetCyberMapAttr -96 */
static struct Library* CyberGfxBase;
#define CGX_WPA(src, sx, sy, mod, rp, dx, dy, w, h, fmt) \
	LP10(0x7e, ULONG, WritePixelArray, APTR, (src), a0, UWORD, (sx), d0, UWORD, (sy), d1, UWORD, (mod), d2, \
	     struct RastPort*, (rp), a1, UWORD, (dx), d3, UWORD, (dy), d4, UWORD, (w), d5, UWORD, (h), d6, \
	     UBYTE, (fmt), d7, , CyberGfxBase)
#define CGX_MAPATTR(bm, a) LP2(0x60, ULONG, GetCyberMapAttr, struct BitMap*, (bm), a0, ULONG, (a), d0, , CyberGfxBase)
#define CYBRMATTR_DEPTH 0x80000007UL
#define CYBRMATTR_ISCYBERGFX 0x80000008UL
#define RECTFMT_RGB 0

static struct ColorMap* s_cm;
static LONG s_pens[27];
static LONG s_white = -1, s_black = -1;
static int s_npens;

static const UBYTE bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };




static void get_pens(struct Screen* scr)
{
	static const ULONG lv[3] = { 0, 0x80000000UL, 0xFFFFFFFFUL };
	int r, g, b;
	if (s_cm)
		return;
	s_cm = scr->ViewPort.ColorMap;
	for (r = 0; r < 3; r++)
		for (g = 0; g < 3; g++)
			for (b = 0; b < 3; b++)
			{
				/* a shared screen may have no pen to spare: then the nearest it has */
				LONG pen = ObtainBestPen(s_cm, lv[r], lv[g], lv[b], OBP_Precision, PRECISION_IMAGE, TAG_DONE);
				s_pens[s_npens++] = pen >= 0 ? pen : -2 - FindColor(s_cm, lv[r], lv[g], lv[b], -1);
			}
	s_white = ObtainBestPen(s_cm, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, TAG_DONE);
	s_black = ObtainBestPen(s_cm, 0, 0, 0, TAG_DONE);
}

void desktop_free(void)
{
	int i;
	if (s_cm)
	{
		for (i = 0; i < s_npens; i++)
			if (s_pens[i] >= 0) ReleasePen(s_cm, (ULONG)s_pens[i]); /* -2 - n: FindColor's n, not obtained */
		if (s_white >= 0) ReleasePen(s_cm, (ULONG)s_white);
		if (s_black >= 0) ReleasePen(s_cm, (ULONG)s_black);
	}
	s_cm = NULL;
	s_npens = 0;
	s_white = s_black = -1;
	if (CyberGfxBase)
		CloseLibrary(CyberGfxBase);
	CyberGfxBase = NULL;
}

/* 0..255 to 0..2 with the dither threshold t (0..15) */
static int q3(int v, int t)
{
	int s = v * 2 * 16 / 255 + t; /* in 16ths of a step */
	s /= 16;
	return s > 2 ? 2 : s;
}

void desktop_draw(struct Window* win, int x0, int y0, int w, int h, const struct session_info* info)
{
	struct RastPort* rp = win->RPort;
	int W = info->width, H = info->height, x, y, truecolour;
	int dw = w < W ? w : W, dh = h < H ? h : H;
	UBYTE* row;
	char line[320];

	if (w <= 0 || h <= 0)
		return;
	if (!CyberGfxBase)
		CyberGfxBase = OpenLibrary((CONST_STRPTR) "cybergraphics.library", 40);
	truecolour = CyberGfxBase && CGX_MAPATTR(rp->BitMap, CYBRMATTR_ISCYBERGFX) &&
	             CGX_MAPATTR(rp->BitMap, CYBRMATTR_DEPTH) > 8;
	get_pens(win->WScreen); /* the dither's cube, and the text's pens on any screen */

	/* outside the remote desktop, when the window is the bigger: the theme's background pen */
	SetAPen(rp, 0);
	if (w > dw)
		RectFill(rp, x0 + dw, y0, x0 + w - 1, y0 + h - 1);
	if (h > dh)
		RectFill(rp, x0, y0 + dh, x0 + dw - 1, y0 + h - 1);

	row = AllocVec((ULONG)dw * 3, MEMF_ANY);
	if (!row)
		return;
	for (y = 0; y < dh; y++)
	{
		if (truecolour)
		{
			UBYTE* p = row;
			for (x = 0; x < dw; x++)
			{
				ULONG c = ui_pattern(x, y, W, H);
				*p++ = (UBYTE)(c >> 16);
				*p++ = (UBYTE)(c >> 8);
				*p++ = (UBYTE)c;
			}
			CGX_WPA(row, 0, 0, (UWORD)(dw * 3), rp, (UWORD)x0, (UWORD)(y0 + y), (UWORD)dw, 1, RECTFMT_RGB);
		}
		else
		{
			for (x = 0; x < dw; x++)
			{
				ULONG c = ui_pattern(x, y, W, H);
				int t = bayer[y & 3][x & 3];
				int i = q3((c >> 16) & 255, t) * 9 + q3((c >> 8) & 255, t) * 3 + q3(c & 255, t);
				row[x] = (UBYTE)(s_pens[i] >= 0 ? s_pens[i] : -2 - s_pens[i]);
			}
			WriteChunkyPixels(rp, (UWORD)x0, (UWORD)(y0 + y), (UWORD)(x0 + dw - 1), (UWORD)(y0 + y), row, dw);
		}
	}
	FreeVec(row);

	/* the words in the first mock window's title bar and client area */
	SetDrMd(rp, JAM1);
	SetAPen(rp, (ULONG)(s_white >= 0 ? s_white : 2));
	Move(rp, x0 + W / 12 + 6, y0 + H / 10 + 4 + rp->TxBaseline);
	Text(rp, (STRPTR) "Remote desktop", 14);
	SetAPen(rp, (ULONG)(s_black >= 0 ? s_black : 1));
	sprintf(line, "%s: a test pattern, %d x %d", info->demo ? "Demo session" : "Not drawn yet", W, H);
	Move(rp, x0 + W / 12 + 8, y0 + H / 10 + 30 + rp->TxBaseline);
	Text(rp, (STRPTR)line, (ULONG)strlen(line));
	sprintf(line, "Computer: %s", info->host);
	Move(rp, x0 + W / 12 + 8, y0 + H / 10 + 30 + rp->TxHeight * 2 + rp->TxBaseline);
	Text(rp, (STRPTR)line, (ULONG)strlen(line));
}
