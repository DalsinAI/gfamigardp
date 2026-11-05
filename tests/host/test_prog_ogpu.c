/* Host test: RemoteFX Progressive decode through OpenGPU's v1.10 ops
 * (RLGR_DEC, RFX_TILE, RFX_YCBCR) against progressive.c's own CPU
 * kernels, pixel for pixel. The accel hooks here stand in for
 * src/amiga/ogpu_prog.c: they run gfopenrtg's core (library/opengpu,
 * ogpu_core.c + ogpu_dwt.c + ogpu_build.c, from GFOPENRTG) on an arena
 * whose buffers are big-endian, as on the Amiga.
 *   cc -std=c99 -Isrc/codec -I$GFOPENRTG/include -I$GFOPENRTG/library/opengpu \
 *      tests/host/test_prog_ogpu.c src/codec/progressive.c src/codec/gfx_fb.c \
 *      $GFOPENRTG/library/opengpu/ogpu_core.c $GFOPENRTG/library/opengpu/ogpu_dwt.c \
 *      $GFOPENRTG/library/opengpu/ogpu_build.c
 * (tools/test_prog_ogpu.sh does this.) Streams are synthetic: random RLGR,
 * SRL and raw bytes in well-formed blocks, with random rectangles, quant
 * and progressive quality, both DWT layouts.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "progressive.h"
#include "ogpu_core.h"
#include "gfopengpu/build.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

/* ---- the arena and the core ---- */
#define ARENA (1L << 20)
static ogpu_u8 arena[ARENA];
#define SRC_A   0x00000UL   /* 3 x 16 KB of RLGR bytes */
#define VAL_A   0x0C000UL   /* 3 x 4096 int16 */
#define COEF_A  0x12000UL
#define QUANT_A 0x18000UL
#define YCC_A   0x20000UL
#define WORK_A  0x30000UL
#define CANVAS_A 0x40000UL  /* 64 x 64 ARGB32 */

static ogpu_u8* map(void* user, ogpu_u32 a, ogpu_u32 n)
{
	(void)user;
	return a < ARENA && n <= (ogpu_u32)ARENA - a ? arena + a : NULL;
}

static int core_run(unsigned char* s, long words)
{
	struct ogpu_core C;
	ogpu_core_init(&C);
	C.map = map;
	ogpu_core_run(&C, s, words);
	return C.last_error;
}

static int calls_rlgr, calls_tile;

static int shim_rlgr(void* user, const uint8_t* const src[3], const uint16_t len[3], int re, int16_t* out)
{
	unsigned char sb[128];
	struct OGPUBatch B;
	int c, i, r;
	(void)user;
	calls_rlgr++;
	ogpu_batch_init(&B, sb, sizeof sb / 4);
	for (c = 0; c < 3; c++)
	{
		memcpy(arena + SRC_A + c * 16384, src[c], len[c]);
		ogpu_rlgr_dec(&B, SRC_A + (unsigned long)c * 16384, len[c], VAL_A + (unsigned long)c * 8192, 4096,
		              OGPU_RLGR_LL3_DELTA | (re ? OGPU_RFX_RE : 0));
	}
	r = core_run(sb, B.words);
	if (r == OGPU_ERR_DATA)
		return RDP_PROG_E_PROTO;
	if (r != OGPU_OK)
		return 0;
	for (i = 0; i < 3 * 4096; i++)
		out[i] = (int16_t)(uint16_t)((arena[VAL_A + i * 2] << 8) | arena[VAL_A + i * 2 + 1]);
	return 1;
}

static int shim_tile(void* user, const int16_t* coef, const uint8_t quant[3][10], int re,
                     int32_t* ycc, int32_t* tmp, uint32_t* px, uint16_t w, int tx, int ty,
                     const uint16_t* rects, int nrects)
{
	unsigned char sb[64];
	struct OGPUBatch B;
	int i, r, x, y;
	(void)user; (void)ycc; (void)tmp;
	calls_tile++;
	for (i = 0; i < 3 * 4096; i++)
	{
		arena[COEF_A + i * 2] = (ogpu_u8)((uint16_t)coef[i] >> 8);
		arena[COEF_A + i * 2 + 1] = (ogpu_u8)coef[i];
	}
	memcpy(arena + QUANT_A, &quant[0][0], 30);
	ogpu_batch_init(&B, sb, sizeof sb / 4);
	ogpu_rfx_tile(&B, COEF_A, QUANT_A, re ? OGPU_RFX_RE : 0, YCC_A, WORK_A, OGPU_RFX_TILE_WORK_BYTES);
	if (core_run(sb, B.words) != OGPU_OK)
		return 0;
	for (r = 0; r < nrects; r++)
	{
		const uint16_t* q = rects + r * 4;
		int x0 = q[0] - tx, y0 = q[1] - ty, rw = q[2] - q[0], rh = q[3] - q[1];
		ogpu_batch_init(&B, sb, sizeof sb / 4);
		ogpu_rfx_ycbcr(&B, YCC_A, x0, y0, rw, rh, CANVAS_A + (unsigned long)(y0 * 64 + x0) * 4, 256);
		if (core_run(sb, B.words) != OGPU_OK)
			return 0;
		for (y = 0; y < rh; y++)
			for (x = 0; x < rw; x++)
			{
				const ogpu_u8* p = arena + CANVAS_A + ((y0 + y) * 64 + x0 + x) * 4;
				px[(size_t)(q[1] + y) * w + q[0] + x] =
				    ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
			}
	}
	return 1;
}

/* ---- synthetic streams ---- */
static uint32_t rng = 7;
static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

static uint8_t* put16(uint8_t* p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); return p + 2; }
static uint8_t* put32(uint8_t* p, uint32_t v) { p = put16(p, v & 0xFFFF); return put16(p, v >> 16); }

static uint8_t* rand_bytes(uint8_t* p, int n)
{
	/* mostly sparse (small values, long zero runs: tiles that don't just
	 * saturate), sometimes dense */
	int i, d = (int)(rnd() % 4);
	for (i = 0; i < n; i++)
	{
		uint32_t v = rnd() & 255;
		if (d != 3) v &= rnd() & rnd() & 255;
		if (d == 0) v &= rnd() & 255;
		p[i] = (uint8_t)v;
	}
	return p + n;
}

/* One frame: a region with nrects rectangles, 2 quants, 2 progressive
 * qualities and ntiles tiles of the given kind (0 simple, 1 first, 2 upgrade). */
static size_t make_region(uint8_t* buf, int re, int kind, int gw, int gh, int ntiles, const int* tx, const int* ty)
{
	uint8_t *p = buf, *len_at, *tsize_at, *tiles;
	int nrects = 1 + (int)(rnd() % 4), i, k, c;
	p = put16(p, 0xCCC4);
	len_at = p; p += 4;
	*p++ = 64;                 /* tileSize */
	p = put16(p, (unsigned)nrects);
	*p++ = 2;                  /* numQuant */
	*p++ = 2;                  /* numProgQuant */
	*p++ = (uint8_t)(re ? 1 : 0);
	p = put16(p, (unsigned)ntiles);
	tsize_at = p; p += 4;
	for (i = 0; i < nrects; i++)
	{
		int x = (int)(rnd() % (unsigned)(gw * 64)), y = (int)(rnd() % (unsigned)(gh * 64));
		int w = 1 + (int)(rnd() % (unsigned)(gw * 64 - x)), h = 1 + (int)(rnd() % (unsigned)(gh * 64 - y));
		if (i == 0 && rnd() % 2) { x = y = 0; w = gw * 64; h = gh * 64; }
		p = put16(p, (unsigned)x); p = put16(p, (unsigned)y); p = put16(p, (unsigned)w); p = put16(p, (unsigned)h);
	}
	for (i = 0; i < 2 * 5; i++)
		*p++ = (uint8_t)((6 + rnd() % 5) | ((6 + rnd() % 5) << 4));
	for (i = 0; i < 2; i++)
	{
		*p++ = (uint8_t)i;
		for (k = 0; k < 15; k++)
			*p++ = (uint8_t)((rnd() % (i ? 2 : 4)) | ((rnd() % (i ? 2 : 4)) << 4));
	}
	tiles = p;
	for (k = 0; k < ntiles; k++)
	{
		uint8_t* t = p;
		int len[6];
		p = put16(p, kind == 0 ? 0xCCC5 : kind == 1 ? 0xCCC6 : 0xCCC7);
		p += 4;
		*p++ = (uint8_t)(rnd() % 2); *p++ = (uint8_t)(rnd() % 2); *p++ = (uint8_t)(rnd() % 2);
		p = put16(p, (unsigned)tx[k]); p = put16(p, (unsigned)ty[k]);
		if (kind == 2)
		{
			*p++ = (uint8_t)(rnd() % 3 == 0 ? 0xFF : 1);   /* quality: final or the finer step */
			for (c = 0; c < 3; c++)
			{
				len[c * 2] = 20 + (int)(rnd() % 400);
				len[c * 2 + 1] = 20 + (int)(rnd() % 400);
				p = put16(p, (unsigned)len[c * 2]);
				p = put16(p, (unsigned)len[c * 2 + 1]);
			}
			for (c = 0; c < 6; c++)
				p = rand_bytes(p, len[c]);
		}
		else
		{
			*p++ = (uint8_t)(kind == 1 ? 0 : (rnd() % 2));           /* flags: RFX_TILE_DIFFERENCE */
			if (kind == 1)
				*p++ = 0;                                             /* quality 0 (coarse) */
			for (c = 0; c < 3; c++)
			{
				len[c] = 200 + (int)(rnd() % 3000);
				p = put16(p, (unsigned)len[c]);
			}
			p = put16(p, 0);                                          /* tailLen */
			for (c = 0; c < 3; c++)
				p = rand_bytes(p, len[c]);
		}
		put32(t + 2, (uint32_t)(p - t));
	}
	put32(tsize_at, (uint32_t)(p - tiles));
	put32(len_at, (uint32_t)(p - buf));
	return (size_t)(p - buf);
}

static long s_mid, s_all;

static int run_frames(int re, int seed, int use_clip)
{
	static uint8_t frame[200000];
	enum { GW = 3, GH = 2, W = GW * 64 - 17, H = GH * 64 - 9 };
	struct rdp_prog_surface ps[2];
	struct rdp_prog_stats st[2];
	struct rdp_prog_clip clip = { 20, 10, 150, 100 };
	struct rdp_prog_accel acc;
	uint32_t* px[2];
	int pass, f, rc[2], i, diff = 0, mid = 0, tx[6], ty[6];
	memset(&acc, 0, sizeof(acc));
	acc.rlgr = shim_rlgr;
	acc.tile = shim_tile;
	for (pass = 0; pass < 2; pass++)
	{
		memset(&ps[pass], 0, sizeof(ps[pass]));
		memset(&st[pass], 0, sizeof(st[pass]));
		px[pass] = calloc((size_t)W * H, 4);
	}
	for (f = 0; f < 6; f++)
	{
		int kind = f == 0 ? 0 : f < 3 ? 1 : 2, n = 0;
		size_t len;
		for (i = 0; i < GW * GH; i++)
			if (rnd() % 3 || kind != 0)
			{
				tx[n] = i % GW;
				ty[n] = i / GW;
				n++;
			}
		{
			uint32_t keep = rng;
			len = make_region(frame, re, kind, GW, GH, n, tx, ty);
			(void)keep;
		}
		for (pass = 0; pass < 2; pass++)
		{
			rdp_progressive_set_accel(pass ? &acc : NULL);
			rc[pass] = rdp_progressive_decode(&ps[pass], NULL, frame, len, px[pass], W, H, NULL, NULL,
			                                  &st[pass], use_clip ? &clip : NULL);
		}
		CHECK(rc[0] == rc[1], "seed %d frame %d: rc %d vs %d", seed, f, rc[0], rc[1]);
	}
	if (use_clip)
		for (pass = 0; pass < 2; pass++)
		{
			rdp_progressive_set_accel(pass ? &acc : NULL);
			rdp_progressive_paint(&ps[pass], NULL, px[pass], W, H, 0, 0, W, H, NULL, NULL, &st[pass]);
		}
	rdp_progressive_set_accel(NULL);
	for (i = 0; i < W * H; i++)
	{
		uint32_t v = px[1][i];
		diff += px[0][i] != v;
		mid += ((v >> 16) & 255) > 0 && ((v >> 16) & 255) < 255 && (v & 255) > 0 && (v & 255) < 255;
	}
	s_mid += mid;
	s_all += W * H;
	CHECK(diff == 0, "seed %d re %d clip %d: %d pixels differ", seed, re, use_clip, diff);
	CHECK(st[0].simple == st[1].simple && st[0].first == st[1].first && st[0].upgrade == st[1].upgrade &&
	      st[0].skipped == st[1].skipped && st[0].painted == st[1].painted,
	      "seed %d: stats differ", seed);
	CHECK(st[1].accel_tiles > 0 && st[1].accel_rlgr > 0 && st[1].cpu_fallback == 0 && st[0].accel_tiles == 0,
	      "seed %d: accel %u/%u fallback %u", seed, (unsigned)st[1].accel_tiles, (unsigned)st[1].accel_rlgr,
	      (unsigned)st[1].cpu_fallback);
	for (pass = 0; pass < 2; pass++)
	{
		rdp_progressive_free(&ps[pass], NULL);
		free(px[pass]);
	}
	return (int)st[1].accel_tiles;
}

int main(int argc, char** argv)
{
	int seeds = argc > 1 ? atoi(argv[1]) : 40, s, tiles = 0;
	for (s = 0; s < seeds; s++)
	{
		rng = (uint32_t)(s * 7919 + 1);
		tiles += run_frames(s & 1, s, (s >> 1) & 1);
	}
	printf("progressive via OpenGPU: %d seeds, %d tiles through RFX_TILE, %d RLGR batches, %ld%% of pixels unsaturated, %s\n",
	       seeds, tiles, calls_rlgr, s_all ? s_mid * 100 / s_all : 0, fails ? "MISMATCH" : "pixel-exact with the CPU path");
	CHECK(s_mid * 10 > s_all, "too few unsaturated pixels to tell paths apart");
	return fails != 0;
}
