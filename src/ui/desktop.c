/*
 * GFAmigaRDP: the session window's desktop area. Until the RDP output is
 * written (docs/DESIGN.md 6) it shows a test pattern at the remote size:
 * a gradient, a grid, two mock windows, a grey ramp and colour bars.
 *
 * Drawing, best first (DESIGN.md Drawing): gfopengpu PIXELS/COPY when
 * OGPU_Query allows and LockBitMapTags gives the BitMap address; else on a
 * true-colour screen WritePixelArray (RECTFMT_ARGB/RGB); on a palette
 * screen a 3x3x3 cube with ordered dither via WriteChunkyPixels (V40).
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

#include <utility/tagitem.h>
#include <graphics/layers.h>
#include <gfopengpu/gfopengpu.h>
#include <gfopengpu/stream.h>
#include <proto/gfopengpu.h>


/* cybergraphics.library by its vectors (interface facts from its
 * autodoc): WritePixelArray -126, GetCyberMapAttr -96 */
static struct Library* CyberGfxBase;
extern struct Library* OpenGPUBase; /* defined in ogpu_prog.c */
#define CGX_WPA(src, sx, sy, mod, rp, dx, dy, w, h, fmt) \
	LP10(0x7e, ULONG, WritePixelArray, APTR, (src), a0, UWORD, (sx), d0, UWORD, (sy), d1, UWORD, (mod), d2, \
	     struct RastPort*, (rp), a1, UWORD, (dx), d3, UWORD, (dy), d4, UWORD, (w), d5, UWORD, (h), d6, \
	     UBYTE, (fmt), d7, , CyberGfxBase)
#define CGX_MAPATTR(bm, a) LP2(0x60, ULONG, GetCyberMapAttr, struct BitMap*, (bm), a0, ULONG, (a), d0, , CyberGfxBase)
/* LockBitMapTagList -168, UnLockBitMap -174 (cybergraphics.library) */
#define CGX_LOCKBM(bm, tags) LP2(0xa8, APTR, LockBitMapTagList, struct BitMap*, (bm), a0, struct TagItem*, (tags), a1, , CyberGfxBase)
#define CGX_UNLOCKBM(h) LP1NR(0xae, UnLockBitMap, APTR, (h), a0, , CyberGfxBase)
#define CYBRMATTR_DEPTH 0x80000007UL
#define CYBRMATTR_ISCYBERGFX 0x80000008UL
#define CYBRMATTR_PIXFMT 0x80000004UL
#define CYBRMATTR_XMOD 0x80000001UL
#define RECTFMT_RGB 0
#define RECTFMT_ARGB 2
#define LBMI_BYTESPERROW 0x84001006UL
#define LBMI_BASEADDRESS 0x84001007UL
#define LBMI_PIXFMT 0x84001004UL
#define PIXFMT_RGB16 5UL
#define PIXFMT_ARGB32 11UL
#define PIXFMT_BGRA32 12UL
#define PIXFMT_RGBA32 13UL

static int s_ogpu_pixels; /* 1: PIXELS ok for ARGB32 target */
static int s_ogpu_copy;   /* 1: COPY ok for ARGB32 target */
static int s_ogpu_queried; /* 1: PIXELS/COPY Query done (base shared with ogpu_prog.c) */

static void ogpu_ensure(void)
{
	ULONG q;
	if (s_ogpu_queried)
		return;
	if (!OpenGPUBase)
		OpenGPUBase = OpenLibrary((CONST_STRPTR)OPENGPU_NAME, OPENGPU_VERSION);
	if (!OpenGPUBase)
		return;
	q = OGPU_Query(OGPU_OP_PIXELS, OGPU_FMT_ARGB32);
	s_ogpu_pixels = OGPU_ANSWER(q) != OGPU_NONE;
	q = OGPU_Query(OGPU_OP_COPY, OGPU_FMT_ARGB32);
	s_ogpu_copy = OGPU_ANSWER(q) != OGPU_NONE;
	s_ogpu_queried = 1;
	/* Keep the base even when blit Query fails: ogpu_prog may still use the RFX ops. */
}

/* Map a CyberGraphX PIXFMT to an OGPU surface format; 0 if unsupported. */
static int ogpu_fmt_of_pixfmt(ULONG pf)
{
	if (pf == PIXFMT_ARGB32 || pf == PIXFMT_BGRA32 || pf == PIXFMT_RGBA32)
		return OGPU_FMT_ARGB32;
	if (pf == PIXFMT_RGB16)
		return OGPU_FMT_RGB565;
	return 0;
}

static struct ColorMap* s_cm;
static LONG s_pens[27];
static LONG s_white = -1, s_black = -1;
static int s_npens;

static const UBYTE bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };




static void get_pens(struct Screen* scr)
{
	static const ULONG lv[3] = { 0, 0x80000000UL, 0xFFFFFFFFUL };
	int r, g, b;
	if (s_cm == scr->ViewPort.ColorMap)
		return;
	desktop_release_pens();   /* the cube was another screen's (full screen and back) */
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

/* Give back the cube's and the text's pens. Called before the screen they
 * came from closes (the full screen's own), and by desktop_free. */
void desktop_release_pens(void)
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
}

void desktop_free(void)
{
	desktop_release_pens();
	if (CyberGfxBase)
		CloseLibrary(CyberGfxBase);
	CyberGfxBase = NULL;
	if (OpenGPUBase)
		CloseLibrary(OpenGPUBase);
	OpenGPUBase = NULL;
	s_ogpu_pixels = s_ogpu_copy = s_ogpu_queried = 0;
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
                  const struct rdp_rect* rects, int n, int ox, int oy)
{
	struct RastPort* rp = win->RPort;
	int i, truecolour;
	UBYTE* row = NULL;
	int vx1, vy1;
	APTR lock = NULL;
	UBYTE* base = NULL;
	ULONG bpr = 0, pixfmt = 0;
	int ogpu_fmt = 0, use_ogpu = 0;
	int layer_ox = 0, layer_oy = 0;
	ULONG stream[8 + 64 * 7];
	ULONG *sw;
	ULONG fence = 0;

	if (w <= 0 || h <= 0 || !fb || !fb->px)
		return;
	if (ox < 0) ox = 0;
	if (oy < 0) oy = 0;
	vx1 = ox + w;
	vy1 = oy + h;
	if (vx1 > fb->w) vx1 = fb->w;
	if (vy1 > fb->h) vy1 = fb->h;
	truecolour = truecolour_rp(rp);
	ogpu_ensure();
	if (truecolour && OpenGPUBase && (s_ogpu_pixels || s_ogpu_copy) && CyberGfxBase && n > 0 && n <= 64)
	{
		struct TagItem tags[4];
		tags[0].ti_Tag = LBMI_BASEADDRESS; tags[0].ti_Data = (ULONG)&base;
		tags[1].ti_Tag = LBMI_BYTESPERROW; tags[1].ti_Data = (ULONG)&bpr;
		tags[2].ti_Tag = LBMI_PIXFMT; tags[2].ti_Data = (ULONG)&pixfmt;
		tags[3].ti_Tag = TAG_DONE;
		lock = CGX_LOCKBM(rp->BitMap, tags);
		if (lock && base && bpr)
		{
			ogpu_fmt = ogpu_fmt_of_pixfmt(pixfmt);
			if (!ogpu_fmt)
				ogpu_fmt = OGPU_FMT_ARGB32; /* hope: many CGX 32-bit screens */
			if (rp->Layer)
			{
				layer_ox = rp->Layer->bounds.MinX;
				layer_oy = rp->Layer->bounds.MinY;
			}
			/* Prefer COPY when both ends are ARGB32; else PIXELS from ARGB FB. */
			if (ogpu_fmt == OGPU_FMT_ARGB32 && s_ogpu_copy)
				use_ogpu = 2;
			else if (s_ogpu_pixels)
				use_ogpu = 1;
		}
		if (!use_ogpu && lock)
		{
			CGX_UNLOCKBM(lock);
			lock = NULL;
			base = NULL;
		}
	}
	if (use_ogpu)
	{
		sw = stream;
		*sw++ = OGPU_HDR(OGPU_OP_SURFACE, 6);
		*sw++ = 0;
		*sw++ = (ULONG)base;
		*sw++ = bpr;
		*sw++ = OGPU_XY((int)(bpr / (ogpu_fmt == OGPU_FMT_RGB565 ? 2 : 4)),
		                (int)(rp->BitMap->Rows > 0 ? rp->BitMap->Rows : h + layer_oy));
		*sw++ = (ULONG)ogpu_fmt;
		if (use_ogpu == 2)
		{
			*sw++ = OGPU_HDR(OGPU_OP_SURFACE, 6);
			*sw++ = 1;
			*sw++ = (ULONG)fb->px;
			*sw++ = (ULONG)fb->stride * 4UL;
			*sw++ = OGPU_XY(fb->w, fb->h);
			*sw++ = OGPU_FMT_ARGB32;
		}
		*sw++ = OGPU_HDR(OGPU_OP_TARGET, 2);
		*sw++ = 0;
		for (i = 0; i < n; i++)
		{
			int rx0 = rects[i].x0, ry0 = rects[i].y0, rx1 = rects[i].x1, ry1 = rects[i].y1;
			int dx, dy, bw, bh;
			if (rx0 < ox) rx0 = ox;
			if (ry0 < oy) ry0 = oy;
			if (rx1 > vx1) rx1 = vx1;
			if (ry1 > vy1) ry1 = vy1;
			if (rx0 >= rx1 || ry0 >= ry1)
				continue;
			dx = x0 + (rx0 - ox) + layer_ox;
			dy = y0 + (ry0 - oy) + layer_oy;
			bw = rx1 - rx0;
			bh = ry1 - ry0;
			if (use_ogpu == 2)
			{
				*sw++ = OGPU_HDR(OGPU_OP_COPY, 5);
				*sw++ = 1;
				*sw++ = OGPU_XY(rx0, ry0);
				*sw++ = OGPU_XY(dx, dy);
				*sw++ = OGPU_XY(bw, bh);
			}
			else
			{
				const uint32_t* src = fb->px + (size_t)ry0 * fb->stride + (size_t)rx0;
				*sw++ = OGPU_HDR(OGPU_OP_PIXELS, 7);
				*sw++ = (ULONG)src;
				*sw++ = (ULONG)fb->stride * 4UL;
				*sw++ = OGPU_FMT_ARGB32;
				*sw++ = 0;
				*sw++ = OGPU_XY(dx, dy);
				*sw++ = OGPU_XY(bw, bh);
			}
		}
		*sw++ = OGPU_HDR(OGPU_OP_FENCE, 2);
		*sw++ = 1;
		if (OGPU_Submit(stream, (ULONG)(sw - stream), &fence) == OGPU_OK)
			OGPU_Wait(fence);
		else
			use_ogpu = 0; /* fall through to WPA for this frame */
		CGX_UNLOCKBM(lock);
		if (use_ogpu)
			return;
		/* Submit failed: fall back to WritePixelArray below. */
	}
	if (!truecolour)
	{
		get_pens(win->WScreen);
		row = AllocVec((ULONG)w + 16, MEMF_ANY);
		if (!row)
			return;
	}
	for (i = 0; i < n; i++)
	{
		int rx0 = rects[i].x0, ry0 = rects[i].y0, rx1 = rects[i].x1, ry1 = rects[i].y1, x, y;
		int dx, dy, bw, bh;
		if (rx0 < ox) rx0 = ox;
		if (ry0 < oy) ry0 = oy;
		if (rx1 > vx1) rx1 = vx1;
		if (ry1 > vy1) ry1 = vy1;
		if (rx0 >= rx1 || ry0 >= ry1)
			continue;
		dx = x0 + (rx0 - ox);
		dy = y0 + (ry0 - oy);
		bw = rx1 - rx0;
		bh = ry1 - ry0;
		if (truecolour)
		{
			CGX_WPA((APTR)fb->px, (UWORD)rx0, (UWORD)ry0, (UWORD)(fb->stride * 4), rp,
			        (UWORD)dx, (UWORD)dy, (UWORD)bw, (UWORD)bh, RECTFMT_ARGB);
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
			WriteChunkyPixels(rp, (UWORD)dx, (UWORD)(dy + (y - ry0)), (UWORD)(dx + bw - 1),
			                  (UWORD)(dy + (y - ry0)), row, bw);
		}
	}
	if (row)
		FreeVec(row);
}

/* All of the visible pan window: the buffer where it is, theme background outside */
void desktop_draw_fb(struct Window* win, int x0, int y0, int w, int h, const struct rdp_fb* fb,
                     int ox, int oy)
{
	struct RastPort* rp = win->RPort;
	struct rdp_rect all;
	int dw, dh;
	if (w <= 0 || h <= 0 || !fb)
		return;
	if (ox < 0) ox = 0;
	if (oy < 0) oy = 0;
	if (ox > fb->w) ox = fb->w;
	if (oy > fb->h) oy = fb->h;
	dw = w < (fb->w - ox) ? w : (fb->w - ox);
	dh = h < (fb->h - oy) ? h : (fb->h - oy);
	SetAPen(rp, 0);
	if (w > dw)
		RectFill(rp, x0 + dw, y0, x0 + w - 1, y0 + h - 1);
	if (h > dh)
		RectFill(rp, x0, y0 + dh, x0 + dw - 1, y0 + h - 1);
	all.x0 = (uint16_t)ox;
	all.y0 = (uint16_t)oy;
	all.x1 = (uint16_t)(ox + dw);
	all.y1 = (uint16_t)(oy + dh);
	desktop_blit(win, x0, y0, w, h, fb, &all, 1, ox, oy);
}
