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
#include "gfx_fb.h"

#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/view.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
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
#define RECTFMT_ARGB 2

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

/* text at (x, y) in a band of w x h at (x0, y0), or as much as fits */
static void band_text(struct RastPort* rp, int x0, int y0, int w, int h, int x, int y, const char* str)
{
	struct TextExtent te;
	ULONG n;
	if (x < 0 || y < 0 || x >= w || y + rp->TxHeight > h)
		return;
	n = TextFit(rp, (STRPTR)str, (ULONG)strlen(str), &te, NULL, 1, (UWORD)(w - x), (UWORD)rp->TxHeight);
	if (!n)
		return;
	Move(rp, x0 + x, y0 + y + rp->TxBaseline);
	Text(rp, (STRPTR)str, n);
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

	/* the words in the first mock window's title bar and client area,
	 * kept inside the drawn band (review M6): a line whose row is not all
	 * in it is left out, and one too long is cut where the band ends */
	SetDrMd(rp, JAM1);
	SetAPen(rp, (ULONG)(s_white >= 0 ? s_white : 2));
	band_text(rp, x0, y0, dw, dh, W / 12 + 6, H / 10 + 4, "Remote desktop");
	SetAPen(rp, (ULONG)(s_black >= 0 ? s_black : 1));
	snprintf(line, sizeof(line), "%s: a test pattern, %d x %d", info->demo ? "Demo session" : "Not drawn yet", W, H);
	band_text(rp, x0, y0, dw, dh, W / 12 + 8, H / 10 + 30, line);
	snprintf(line, sizeof(line), "Computer: %s", info->host);
	band_text(rp, x0, y0, dw, dh, W / 12 + 8, H / 10 + 30 + rp->TxHeight * 2, line);
}

/* 1: the window's bitmap is true colour through cybergraphics.library */
static int truecolour_rp(struct RastPort* rp)
{
	if (!CyberGfxBase)
		CyberGfxBase = OpenLibrary((CONST_STRPTR) "cybergraphics.library", 40);
	return CyberGfxBase && CGX_MAPATTR(rp->BitMap, CYBRMATTR_ISCYBERGFX) &&
	       CGX_MAPATTR(rp->BitMap, CYBRMATTR_DEPTH) > 8;
}

/*
 * The remote desktop from the Graphics Output Buffer: only the rectangles
 * given, clipped to the band of w x h at (x0, y0). On a true-colour screen
 * straight from fb->px with WritePixelArray RECTFMT_ARGB (0xAARRGGBB is
 * A,R,G,B in a 68k's memory, so nothing is converted); on a palette screen
 * row by row through the dither cube and WriteChunkyPixels.
 */
void desktop_blit(struct Window* win, int x0, int y0, int w, int h, const struct rdp_fb* fb,
                  const struct rdp_rect* rects, int n)
{
	struct RastPort* rp = win->RPort;
	int i, truecolour;
	UBYTE* row = NULL;

	if (w <= 0 || h <= 0 || !fb || !fb->px)
		return;
	truecolour = truecolour_rp(rp);
	if (!truecolour)
	{
		get_pens(win->WScreen);
		row = AllocVec((ULONG)fb->w + 16, MEMF_ANY);
		if (!row)
			return;
	}
	for (i = 0; i < n; i++)
	{
		int rx0 = rects[i].x0, ry0 = rects[i].y0, rx1 = rects[i].x1, ry1 = rects[i].y1, x, y;
		if (rx1 > fb->w) rx1 = fb->w;
		if (ry1 > fb->h) ry1 = fb->h;
		if (rx1 > w) rx1 = w;
		if (ry1 > h) ry1 = h;
		if (rx0 >= rx1 || ry0 >= ry1)
			continue;
		if (truecolour)
		{
			CGX_WPA((APTR)fb->px, (UWORD)rx0, (UWORD)ry0, (UWORD)(fb->stride * 4), rp,
			        (UWORD)(x0 + rx0), (UWORD)(y0 + ry0), (UWORD)(rx1 - rx0), (UWORD)(ry1 - ry0), RECTFMT_ARGB);
			continue;
		}
		for (y = ry0; y < ry1; y++)
		{
			const uint32_t* src = fb->px + (size_t)y * fb->stride;
			for (x = rx0; x < rx1; x++)
			{
				uint32_t c = src[x];
				int t = bayer[y & 3][x & 3];
				int k = q3((c >> 16) & 255, t) * 9 + q3((c >> 8) & 255, t) * 3 + q3(c & 255, t);
				row[x - rx0] = (UBYTE)(s_pens[k] >= 0 ? s_pens[k] : -2 - s_pens[k]);
			}
			WriteChunkyPixels(rp, (UWORD)(x0 + rx0), (UWORD)(y0 + y), (UWORD)(x0 + rx1 - 1), (UWORD)(y0 + y),
			                  row, rx1 - rx0);
		}
	}
	if (row)
		FreeVec(row);
}

/* All of it: the buffer where it is, the theme's background outside it */
void desktop_draw_fb(struct Window* win, int x0, int y0, int w, int h, const struct rdp_fb* fb)
{
	struct RastPort* rp = win->RPort;
	struct rdp_rect all;
	int dw, dh;
	if (w <= 0 || h <= 0 || !fb)
		return;
	dw = w < fb->w ? w : fb->w;
	dh = h < fb->h ? h : fb->h;
	SetAPen(rp, 0);
	if (w > dw)
		RectFill(rp, x0 + dw, y0, x0 + w - 1, y0 + h - 1);
	if (h > dh)
		RectFill(rp, x0, y0 + dh, x0 + dw - 1, y0 + h - 1);
	all.x0 = all.y0 = 0;
	all.x1 = (uint16_t)dw;
	all.y1 = (uint16_t)dh;
	desktop_blit(win, x0, y0, w, h, fb, &all, 1);
}
