/*
 * Progressive tile YCbCr → ARGB via OGPU_OP_YUV (I420 + JPEG range).
 *
 * RFX Progressive is 4:4:4; this pilot subsamples Cb/Cr 2×2 into I420 and
 * uses OGPU_YUV_JPEG (full range, +128 offset). Colours will not match the
 * RFX 16.16 matrix bit-exactly — visual QA note in docs/STATUS-openup-opt.txt.
 * Falls back (returns 0) when Query says NONE or Submit fails.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "ogpu_yuv.h"
#include "progressive.h"

#include <proto/exec.h>
#include <string.h>

#include <gfopengpu/gfopengpu.h>
#include <gfopengpu/stream.h>

/* Defined in desktop.c so one OpenLibrary owns the base. */
extern struct Library* OpenGPUBase;

#include <proto/gfopengpu.h>

#define TILE 64

static int s_ready;
static uint8_t s_y[TILE * TILE];
static uint8_t s_u[(TILE / 2) * (TILE / 2)];
static uint8_t s_v[(TILE / 2) * (TILE / 2)];
static ULONG s_stream[48];

static uint8_t clamp8(int32_t v)
{
	if ((uint32_t)v <= 255u)
		return (uint8_t)v;
	return (uint8_t)(v < 0 ? 0 : 255);
}

static int yuv_tile(void* user, uint32_t* px, uint16_t sw, uint16_t sh,
                    const int32_t* Y, const int32_t* Cb, const int32_t* Cr,
                    int tile_x, int tile_y, int x0, int y0, int x1, int y1)
{
	ULONG *w, fence = 0;
	int dx0, dy0, dw, dh, x, y;
	(void)user;
	if (!s_ready || !px || !OpenGPUBase || sw == 0 || sh == 0)
		return 0;
	dx0 = x0 - tile_x;
	dy0 = y0 - tile_y;
	if (dx0 < 0) dx0 = 0;
	if (dy0 < 0) dy0 = 0;
	dw = x1 - tile_x;
	dh = y1 - tile_y;
	if (dw > TILE) dw = TILE;
	if (dh > TILE) dh = TILE;
	dw -= dx0;
	dh -= dy0;
	if (dw <= 0 || dh <= 0)
		return 0;
	if (dx0 & 1) { dx0--; dw++; }
	if (dy0 & 1) { dy0--; dh++; }
	if (dw & 1) dw++;
	if (dh & 1) dh++;
	if (dx0 + dw > TILE) dw = (TILE - dx0) & ~1;
	if (dy0 + dh > TILE) dh = (TILE - dy0) & ~1;
	if (dw < 2 || dh < 2)
		return 0;
	if (tile_x + dx0 + dw > sw) dw = (int)sw - tile_x - dx0;
	if (tile_y + dy0 + dh > sh) dh = (int)sh - tile_y - dy0;
	dw &= ~1;
	dh &= ~1;
	if (dw < 2 || dh < 2)
		return 0;

	for (y = 0; y < dh; y++)
	{
		const int32_t* Yrow = Y + (dy0 + y) * TILE + dx0;
		uint8_t* dst = s_y + y * dw;
		for (x = 0; x < dw; x++)
			dst[x] = clamp8(Yrow[x] + 128);
	}
	for (y = 0; y < dh; y += 2)
	{
		const int32_t* Cb0 = Cb + (dy0 + y) * TILE + dx0;
		const int32_t* Cr0 = Cr + (dy0 + y) * TILE + dx0;
		const int32_t* Cb1 = Cb0 + TILE;
		const int32_t* Cr1 = Cr0 + TILE;
		uint8_t* u = s_u + (y / 2) * (dw / 2);
		uint8_t* v = s_v + (y / 2) * (dw / 2);
		for (x = 0; x < dw; x += 2)
		{
			int32_t cb = (Cb0[x] + Cb0[x + 1] + Cb1[x] + Cb1[x + 1] + 2) >> 2;
			int32_t cr = (Cr0[x] + Cr0[x + 1] + Cr1[x] + Cr1[x + 1] + 2) >> 2;
			u[x / 2] = clamp8(cb + 128);
			v[x / 2] = clamp8(cr + 128);
		}
	}

	w = s_stream;
	*w++ = OGPU_HDR(OGPU_OP_SURFACE, 6);
	*w++ = 0;
	*w++ = (ULONG)px;
	*w++ = (ULONG)sw * 4UL;
	*w++ = OGPU_XY((int)sw, (int)sh);
	*w++ = OGPU_FMT_ARGB32;
	*w++ = OGPU_HDR(OGPU_OP_TARGET, 2);
	*w++ = 0;
	*w++ = OGPU_HDR(OGPU_OP_YUV, 10);
	*w++ = (ULONG)s_y;
	*w++ = (ULONG)dw;
	*w++ = (ULONG)s_u;
	*w++ = (ULONG)(dw / 2);
	*w++ = (ULONG)s_v;
	*w++ = (ULONG)(dw / 2);
	*w++ = OGPU_XY(dw, dh);
	*w++ = OGPU_XY(tile_x + dx0, tile_y + dy0);
	*w++ = OGPU_YUV_FORMAT(OGPU_YUV_I420, OGPU_YUV_JPEG);
	*w++ = OGPU_HDR(OGPU_OP_FENCE, 2);
	*w++ = 1;
	if (OGPU_Submit(s_stream, (ULONG)(w - s_stream), &fence) != OGPU_OK)
		return 0;
	OGPU_Wait(fence);
	return 1;
}

void rdp_ogpu_yuv_install(void)
{
	ULONG q;
	s_ready = 0;
	if (!OpenGPUBase)
		OpenGPUBase = OpenLibrary((CONST_STRPTR)OPENGPU_NAME, OPENGPU_VERSION);
	if (!OpenGPUBase)
		return;
	q = OGPU_Query(OGPU_OP_YUV, OGPU_FMT_ARGB32);
	if (OGPU_ANSWER(q) == OGPU_NONE)
		return;
	s_ready = 1;
	rdp_progressive_set_ycbcr(yuv_tile, NULL);
}

void rdp_ogpu_yuv_remove(void)
{
	rdp_progressive_set_ycbcr(NULL, NULL);
	s_ready = 0;
}

int rdp_ogpu_yuv_ready(void)
{
	return s_ready;
}
