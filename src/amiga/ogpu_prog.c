/*
 * RemoteFX Progressive's compute through OpenGPU (gfopengpu.library 0.27,
 * stream v1.10): OGPU_OP_RLGR_DEC for the three components' entropy decode
 * (with LL3's deltas), and per tile one batch of OGPU_OP_RFX_TILE
 * (dequantize + inverse DWT of Y, Cb, Cr) and an OGPU_OP_RFX_YCBCR for each
 * of the tile's rectangles, straight into the surface. The library's CPU
 * back end runs them on the 68k today, bit-exact with progressive.c's own
 * kernels; a driver (ACRTG.gpu, PiStorm.gpu) that answers them in
 * OGPU_Query takes them over with no change here.
 *
 * Fallback: no library, a Query that says NONE, or a Submit/Wait that fails
 * (anything but OGPU_ERR_DATA) returns 0 and progressive.c does that step on
 * the CPU itself. OGPU_ERR_DATA from RLGR_DEC is a malformed tile
 * (RDP_PROG_E_PROTO), as the CPU decoder would say.
 *
 * The buffers are progressive.c's own (68k memory, big-endian as stream.h
 * asks for v1.10). Tile state and the PDUs stay in progressive.c.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "ogpu_prog.h"
#include "progressive.h"

#include <proto/exec.h>
#include <string.h>

#include <gfopengpu/gfopengpu.h>
#include <gfopengpu/stream.h>

/* Shared with desktop.c (dirty blit); one OpenLibrary owns the base. */
struct Library* OpenGPUBase;

#include <proto/gfopengpu.h>

#define TILE_RECTS 32   /* YCBCRs in the tile's first batch; more go in further batches */

static int s_ready;     /* bit 0 RLGR, bit 1 TILE */
static ULONG s_stream[7 + TILE_RECTS * 6 + 2];

static LONG run(ULONG* end)
{
	ULONG fence = 0;
	LONG r;
	*end++ = OGPU_HDR(OGPU_OP_FENCE, 2);
	*end++ = 1;
	r = OGPU_Submit(s_stream, (ULONG)(end - s_stream), &fence);
	if (r == OGPU_OK)
		r = OGPU_Wait(fence);
	return r;
}

static int rlgr3(void* user, const uint8_t* const src[3], const uint16_t len[3], int re, int16_t* out)
{
	ULONG* w = s_stream;
	ULONG flags = OGPU_RLGR_LL3_DELTA | (re ? OGPU_RFX_RE : 0);
	LONG r;
	int c;
	(void)user;
	if (!(s_ready & 1) || !OpenGPUBase)
		return 0;
	for (c = 0; c < 3; c++)
	{
		*w++ = OGPU_HDR(OGPU_OP_RLGR_DEC, 6);
		*w++ = (ULONG)src[c];
		*w++ = (ULONG)len[c];
		*w++ = (ULONG)(out + c * RDP_PROG_COEFS);
		*w++ = RDP_PROG_COEFS;
		*w++ = flags;
	}
	r = run(w);
	if (r == OGPU_OK)
		return 1;
	return r == OGPU_ERR_DATA ? RDP_PROG_E_PROTO : 0;
}

static ULONG* ycbcr(ULONG* w, const int32_t* ycc, uint32_t* px, uint16_t sw, int tx, int ty, const uint16_t* q)
{
	*w++ = OGPU_HDR(OGPU_OP_RFX_YCBCR, 6);
	*w++ = (ULONG)ycc;
	*w++ = OGPU_XY(q[0] - tx, q[1] - ty);
	*w++ = OGPU_XY(q[2] - q[0], q[3] - q[1]);
	*w++ = (ULONG)(px + (ULONG)q[1] * sw + q[0]);
	*w++ = (ULONG)sw * 4UL;
	return w;
}

static int tile(void* user, const int16_t* coef, const uint8_t quant[3][10], int re,
                int32_t* ycc, int32_t* tmp, uint32_t* px, uint16_t sw, int tx, int ty,
                const uint16_t* rects, int nrects)
{
	ULONG* w = s_stream;
	int r = 0, k;
	(void)user;
	if (!(s_ready & 2) || !OpenGPUBase || !px)
		return 0;
	*w++ = OGPU_HDR(OGPU_OP_RFX_TILE, 7);
	*w++ = (ULONG)coef;
	*w++ = (ULONG)&quant[0][0];
	*w++ = re ? OGPU_RFX_RE : 0;
	*w++ = (ULONG)ycc;
	*w++ = (ULONG)tmp;
	*w++ = OGPU_RFX_TILE_WORK_BYTES;
	do
	{
		for (k = 0; k < TILE_RECTS && r < nrects; k++, r++)
			w = ycbcr(w, ycc, px, sw, tx, ty, rects + r * 4);
		if (run(w) != OGPU_OK)
			return 0;   /* the CPU renders the whole tile again */
		w = s_stream;
	} while (r < nrects);
	return 1;
}

void rdp_ogpu_prog_install(void)
{
	struct rdp_prog_accel a;
	s_ready = 0;
	if (!OpenGPUBase)
		OpenGPUBase = OpenLibrary((CONST_STRPTR)OPENGPU_NAME, OPENGPU_VERSION);
	if (!OpenGPUBase)
		return;
	if (OGPU_ANSWER(OGPU_Query(OGPU_OP_RLGR_DEC, OGPU_FMT_ARGB32)) != OGPU_NONE)
		s_ready |= 1;
	if (OGPU_ANSWER(OGPU_Query(OGPU_OP_RFX_TILE, OGPU_FMT_ARGB32)) != OGPU_NONE &&
	    OGPU_ANSWER(OGPU_Query(OGPU_OP_RFX_YCBCR, OGPU_FMT_ARGB32)) != OGPU_NONE)
		s_ready |= 2;
	if (!s_ready)
		return;
	memset(&a, 0, sizeof(a));
	if (s_ready & 1)
		a.rlgr = rlgr3;
	if (s_ready & 2)
		a.tile = tile;
	rdp_progressive_set_accel(&a);
}

void rdp_ogpu_prog_remove(void)
{
	rdp_progressive_set_accel(NULL);
	s_ready = 0;
}

int rdp_ogpu_prog_ready(void)
{
	return s_ready;
}
