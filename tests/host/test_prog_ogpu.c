/* Host test: RemoteFX Progressive decode through OpenGPU's v1.10 ops
 * (RLGR_DEC, RFX_TILE, RFX_YCBCR) against progressive.c's own CPU
 * kernels, pixel for pixel. The accel hooks here stand in for
 * src/amiga/ogpu_prog.c: they run gfopenrtg's core (library/opengpu,
 * ogpu_core.c + ogpu_dwt.c + ogpu_build.c, from GFOPENRTG) on an arena
 * whose buffers are big-endian, as on the Amiga.
 *
 * The stand-in matches ogpu_prog.c's control flow: TILE_RECTS=32 batches,
 * surface-stride YCBCR writes, OGPU_ERR_DATA from the core's RLGR_DEC maps to
 * RDP_PROG_E_PROTO, and a failed follow-on batch returns 0 so progressive.c
 * redraws the whole tile on the CPU (cpu_fallback). Failures come from the
 * real core (unmapped RLGR_DEC / YCBCR destinations: OGPU_ERR_NOMAP), never
 * a short-cut return; truncated RLGR goes through the core too (it answers
 * OK: zeros past the end, see test_rlgr_truncated). A tile whose rects
 * are all clipped away never reaches the hook (stats.empty_skip).
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
#define CANVAS_A 0x40000UL  /* scratch unused by batched path; kept for size */

#define SRC_SLOT 16384
#define TILE_RECTS 32       /* same as src/amiga/ogpu_prog.c */

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
static int last_rlgr_core;           /* the core's answer to the last RLGR_DEC batch */
static int inject_tile_fail_after;   /* Nth tile Submit (1-based) gets an unmapped YCBCR; 0 = off */
static int tile_submits;
static int tile_core_fails;          /* tile batches the core failed */
static long garbage_px;              /* poisoned canvas pixels copied to the surface */
#define POISON 0xA5A5A5A5u

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
		size_t n = len[c];
		if (n > SRC_SLOT)
			n = SRC_SLOT;   /* stand-in slots are 16 KB; never overrun the next */
		memcpy(arena + SRC_A + (size_t)c * SRC_SLOT, src[c], n);
		if (n < SRC_SLOT)
			memset(arena + SRC_A + (size_t)c * SRC_SLOT + n, 0, SRC_SLOT - n);
		ogpu_rlgr_dec(&B, SRC_A + (unsigned long)c * SRC_SLOT, len[c], VAL_A + (unsigned long)c * 8192, 4096,
		              OGPU_RLGR_LL3_DELTA | (re ? OGPU_RFX_RE : 0));
	}
	r = core_run(sb, B.words);
	last_rlgr_core = r;
	if (r == OGPU_ERR_DATA)
		return RDP_PROG_E_PROTO;   /* ogpu_prog.c: r == OGPU_ERR_DATA -> RDP_PROG_E_PROTO */
	if (r != OGPU_OK)
		return 0;
	for (i = 0; i < 3 * 4096; i++)
		out[i] = (int16_t)(uint16_t)((arena[VAL_A + i * 2] << 8) | arena[VAL_A + i * 2 + 1]);
	return 1;
}

/* Mirrors ogpu_prog.c tile(): RFX_TILE once, then YCBCR in batches of 32,
 * writing straight into the caller's surface (stride = sw pixels). The
 * canvas is poisoned before each batch and copied out whatever the core
 * answers, so a failed batch leaves partial / garbage writes in px, as a
 * driver writing straight to the surface would. */
static int shim_tile(void* user, const int16_t* coef, const uint8_t quant[3][10], int re,
                     int32_t* ycc, int32_t* tmp, uint32_t* px, uint16_t sw, int tx, int ty,
                     const uint16_t* rects, int nrects)
{
	unsigned char sb[7 * 4 + TILE_RECTS * 6 * 4 + 32];
	struct OGPUBatch B;
	int i, x, y, done = 0, k;
	unsigned long canvas = CANVAS_A;
	(void)user; (void)ycc; (void)tmp;
	calls_tile++;
	if (nrects <= 0)
		return 0;   /* ogpu_prog.c: never called so; "not taken" */
	for (i = 0; i < 3 * 4096; i++)
	{
		arena[COEF_A + i * 2] = (ogpu_u8)((uint16_t)coef[i] >> 8);
		arena[COEF_A + i * 2 + 1] = (ogpu_u8)coef[i];
	}
	memcpy(arena + QUANT_A, &quant[0][0], 30);
	/* First batch: RFX_TILE + up to TILE_RECTS YCBCRs into a 64-wide scratch,
	 * then copy out with surface stride (Amiga writes straight to px). */
	do
	{
		int batch = 0, r;
		for (i = 0; i < 64 * 64 * 4; i++)
			arena[canvas + i] = (ogpu_u8)(POISON & 255);
		ogpu_batch_init(&B, sb, sizeof sb / 4);
		if (done == 0)
			ogpu_rfx_tile(&B, COEF_A, QUANT_A, re ? OGPU_RFX_RE : 0, YCC_A, WORK_A, OGPU_RFX_TILE_WORK_BYTES);
		for (k = 0; k < TILE_RECTS && done + k < nrects; k++)
		{
			const uint16_t* q = rects + (done + k) * 4;
			int x0 = q[0] - tx, y0 = q[1] - ty, rw = q[2] - q[0], rh = q[3] - q[1];
			unsigned long out = canvas + (unsigned long)(y0 * 64 + x0) * 4;
			/* the failing batch's first YCBCR points outside the arena: the
			 * core answers OGPU_ERR_NOMAP after running the rest */
			if (k == 0 && inject_tile_fail_after && tile_submits + 1 == inject_tile_fail_after)
				out = 0xFFFFFF00UL;
			ogpu_rfx_ycbcr(&B, YCC_A, x0, y0, rw, rh, out, 256);
			batch++;
		}
		tile_submits++;
		r = core_run(sb, B.words);
		for (k = 0; k < batch; k++)
		{
			const uint16_t* q = rects + (done + k) * 4;
			int x0 = q[0] - tx, y0 = q[1] - ty, rw = q[2] - q[0], rh = q[3] - q[1];
			for (y = 0; y < rh; y++)
				for (x = 0; x < rw; x++)
				{
					const ogpu_u8* p = arena + canvas + ((y0 + y) * 64 + x0 + x) * 4;
					uint32_t v = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
					garbage_px += v == POISON;
					px[(size_t)(q[1] + y) * sw + q[0] + x] = v;
				}
		}
		if (r != OGPU_OK)
		{
			tile_core_fails++;
			return 0;   /* ogpu_prog.c: run() != OGPU_OK -> the CPU renders the whole tile again */
		}
		done += batch;
	} while (done < nrects);
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
static size_t make_region(uint8_t* buf, int re, int kind, int gw, int gh, int ntiles, const int* tx, const int* ty,
                          int force_nrects)
{
	uint8_t *p = buf, *len_at, *tsize_at, *tiles;
	int nrects = force_nrects > 0 ? force_nrects : 1 + (int)(rnd() % 4), i, k, c;
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
		int x, y, w, h;
		if (force_nrects > 0)
		{
			/* Pack tiny rects inside the first tile so one tile sees >32. */
			x = (i % 8) * 4;
			y = (i / 8) * 4;
			if (y > 60) y = 60;
			w = 2;
			h = 2;
		}
		else
		{
			x = (int)(rnd() % (unsigned)(gw * 64));
			y = (int)(rnd() % (unsigned)(gh * 64));
			w = 1 + (int)(rnd() % (unsigned)(gw * 64 - x));
			h = 1 + (int)(rnd() % (unsigned)(gh * 64 - y));
			if (i == 0 && rnd() % 2) { x = y = 0; w = gw * 64; h = gh * 64; }
		}
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
	inject_tile_fail_after = 0;
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
		len = make_region(frame, re, kind, GW, GH, n, tx, ty, 0);
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
	      st[0].skipped == st[1].skipped && st[0].painted == st[1].painted && st[0].empty_skip == st[1].empty_skip,
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

/* One region of one simple tile at (0, 0) with a whole-tile rect, whose
 * three components are given byte for byte. */
static size_t make_simple_tile(uint8_t* buf, const uint8_t* const comp[3], const int clen[3])
{
	uint8_t *p = buf, *len_at, *tsize_at, *tiles, *t;
	int i, c;
	p = put16(p, 0xCCC4);
	len_at = p; p += 4;
	*p++ = 64;
	p = put16(p, 1);
	*p++ = 1; *p++ = 1; *p++ = 0;
	p = put16(p, 1);
	tsize_at = p; p += 4;
	p = put16(p, 0); p = put16(p, 0); p = put16(p, 64); p = put16(p, 64);
	for (i = 0; i < 5; i++)
		*p++ = 0x66;
	*p++ = 0;
	for (i = 0; i < 15; i++)
		*p++ = 0;
	tiles = p;
	t = p;
	p = put16(p, 0xCCC5);
	p += 4;
	*p++ = 0; *p++ = 0; *p++ = 0;
	p = put16(p, 0); p = put16(p, 0);
	*p++ = 0;
	for (c = 0; c < 3; c++)
		p = put16(p, (unsigned)clen[c]);
	p = put16(p, 0);
	for (c = 0; c < 3; c++)
	{
		memcpy(p, comp[c], (size_t)clen[c]);
		p += clen[c];
	}
	put32(t + 2, (uint32_t)(p - t));
	put32(tsize_at, (uint32_t)(p - tiles));
	put32(len_at, (uint32_t)(p - buf));
	return (size_t)(p - buf);
}

/* Truncated RLGR1 through the real core. [MS-RDPRFX] RLGR reads zeros past
 * the end, and the decoder only gives up 64 bits past it; for <= 4096
 * coefficients the zero runs finish the component within 62 bits (k = 0 with
 * kr = 10 at the end costs at most 11 + 11 + 10 + 10 bits, then the runs
 * double every 2 bits: 20 more), so a short stream is never OGPU_ERR_DATA
 * in the core, nor an error in rdp_rlgr1_decode. The 10 bytes below are the
 * worst case a search found (62 bits over). Both paths must decode it, to
 * the same pixels. */
static int run_simple(const uint8_t* frame, size_t len, struct rdp_prog_accel* acc, uint32_t* px,
                      struct rdp_prog_stats* st)
{
	struct rdp_prog_surface ps;
	int rc;
	memset(&ps, 0, sizeof(ps));
	memset(st, 0, sizeof(*st));
	memset(px, 0, 64 * 64 * 4);
	rdp_progressive_set_accel(acc);
	rc = rdp_progressive_decode(&ps, NULL, frame, len, px, 64, 64, NULL, NULL, st, NULL);
	rdp_progressive_set_accel(NULL);
	rdp_progressive_free(&ps, NULL);
	return rc;
}

static void test_rlgr_truncated(void)
{
	static uint8_t frame[4096];
	static const uint8_t worst[10] = { 0xBF, 0xDF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB };
	static const uint8_t ones[3] = { 0xFF, 0xFF, 0xFF };
	static const uint8_t zero[1] = { 0 };
	static uint32_t px[2][64 * 64];
	const uint8_t* comp[3];
	int clen[3], rc[2], i, diff = 0, t;
	struct rdp_prog_stats st[2];
	struct rdp_prog_accel acc;
	memset(&acc, 0, sizeof(acc));
	acc.rlgr = shim_rlgr;
	acc.tile = shim_tile;
	for (t = 0; t < 3; t++)
	{
		comp[0] = t == 0 ? worst : zero; clen[0] = t == 0 ? (int)sizeof worst : (int)sizeof zero;
		comp[1] = t == 1 ? worst : ones; clen[1] = t == 1 ? (int)sizeof worst : (int)sizeof ones;
		comp[2] = t == 2 ? worst : zero; clen[2] = t == 2 ? (int)sizeof worst : 0;
		{
			size_t len = make_simple_tile(frame, comp, clen);
			rc[0] = run_simple(frame, len, NULL, px[0], &st[0]);
			last_rlgr_core = -999;
			rc[1] = run_simple(frame, len, &acc, px[1], &st[1]);
		}
		for (i = 0; i < 64 * 64; i++)
			diff += px[0][i] != px[1][i];
		CHECK(rc[0] == 0 && rc[1] == 0, "truncated RLGR %d: rc %d / %d", t, rc[0], rc[1]);
		CHECK(last_rlgr_core == OGPU_OK && st[1].accel_rlgr == 1 && st[1].cpu_fallback == 0,
		      "truncated RLGR %d: core %d accel_rlgr %u", t, last_rlgr_core, (unsigned)st[1].accel_rlgr);
	}
	CHECK(diff == 0, "truncated RLGR: %d pixels differ", diff);
	printf("  truncated RLGR (62 bits past the end) -> core OGPU_OK, pixel-exact with the CPU: ok\n");
}

/* A real core error from RLGR_DEC other than OGPU_ERR_DATA (here
 * OGPU_ERR_NOMAP: the output address is outside the arena) returns 0 from
 * the hook; progressive.c decodes on the CPU and the pixels match. The
 * hook's OGPU_ERR_DATA -> RDP_PROG_E_PROTO branch is kept for drivers, but
 * the core can't answer ERR_DATA for 4096 coefficients (see above). */
static int rlgr_nomap;
static int shim_rlgr_nomap(void* user, const uint8_t* const src[3], const uint16_t len[3], int re, int16_t* out)
{
	unsigned char sb[128];
	struct OGPUBatch B;
	int c, r;
	(void)user; (void)out;
	ogpu_batch_init(&B, sb, sizeof sb / 4);
	for (c = 0; c < 3; c++)
	{
		size_t n = len[c] > SRC_SLOT ? SRC_SLOT : len[c];
		memcpy(arena + SRC_A + (size_t)c * SRC_SLOT, src[c], n);
		ogpu_rlgr_dec(&B, SRC_A + (unsigned long)c * SRC_SLOT, len[c],
		              c == 1 ? 0xFFFFF000UL : VAL_A + (unsigned long)c * 8192, 4096,
		              OGPU_RLGR_LL3_DELTA | (re ? OGPU_RFX_RE : 0));
	}
	r = core_run(sb, B.words);
	last_rlgr_core = r;
	if (r == OGPU_OK)
		return 1;
	rlgr_nomap += r == OGPU_ERR_NOMAP;
	return r == OGPU_ERR_DATA ? RDP_PROG_E_PROTO : 0;   /* as ogpu_prog.c rlgr1() */
}

static void test_rlgr_core_error(void)
{
	static uint8_t frame[200000];
	static uint32_t px[2][64 * 64];
	int tx[1] = { 0 }, ty[1] = { 0 }, rc[2], i, diff = 0;
	struct rdp_prog_stats st[2];
	struct rdp_prog_accel acc;
	size_t len;
	memset(&acc, 0, sizeof(acc));
	acc.rlgr = shim_rlgr_nomap;
	acc.tile = shim_tile;
	rng = 99;
	len = make_region(frame, 0, 0, 1, 1, 1, tx, ty, 0);
	rlgr_nomap = 0;
	rc[0] = run_simple(frame, len, NULL, px[0], &st[0]);
	rc[1] = run_simple(frame, len, &acc, px[1], &st[1]);
	for (i = 0; i < 64 * 64; i++)
		diff += px[0][i] != px[1][i];
	CHECK(rc[0] == 0 && rc[1] == 0, "RLGR core error: rc %d / %d", rc[0], rc[1]);
	CHECK(last_rlgr_core == OGPU_ERR_NOMAP && rlgr_nomap == 1, "RLGR core error: core %d", last_rlgr_core);
	CHECK(st[1].accel_rlgr == 0 && st[1].cpu_fallback == 1 && st[1].accel_tiles == 1,
	      "RLGR core error: accel_rlgr %u fallback %u accel_tiles %u", (unsigned)st[1].accel_rlgr,
	      (unsigned)st[1].cpu_fallback, (unsigned)st[1].accel_tiles);
	CHECK(diff == 0, "RLGR core error: %d pixels differ", diff);
	printf("  RLGR_DEC core error (NOMAP) -> CPU RLGR, pixel-exact: ok\n");
}

/* Follow-on batch fails in the real core (unmapped YCBCR destination) after
 * the first batch and part of the second wrote into px (and the failing
 * rect leaves poison there): progressive.c redraws the whole tile on the
 * CPU, and the surface must end up equal to a CPU-only decode. */
static void test_tile_batch_fail(void)
{
	static uint8_t frame[400000];
	enum { GW = 2, GH = 2, W = 128, H = 128 };
	struct rdp_prog_surface ps[2];
	struct rdp_prog_stats st[2];
	struct rdp_prog_accel acc;
	uint32_t* px[2];
	int tx[1] = { 0 }, ty[1] = { 0 }, rc[2], pass, i, diff = 0, fails_before;
	long garbage_before;
	size_t len;
	memset(&acc, 0, sizeof(acc));
	acc.rlgr = shim_rlgr;
	acc.tile = shim_tile;
	rng = 123;
	len = make_region(frame, 0, 0, GW, GH, 1, tx, ty, 40);   /* >32 rects: two batches */
	for (pass = 0; pass < 2; pass++)
	{
		memset(&ps[pass], 0, sizeof(ps[pass]));
		memset(&st[pass], 0, sizeof(st[pass]));
		px[pass] = calloc((size_t)W * H, 4);
	}
	inject_tile_fail_after = 0;
	rdp_progressive_set_accel(NULL);
	rc[0] = rdp_progressive_decode(&ps[0], NULL, frame, len, px[0], W, H, NULL, NULL, &st[0], NULL);
	tile_submits = 0;
	fails_before = tile_core_fails;
	garbage_before = garbage_px;
	inject_tile_fail_after = 2;   /* first Submit (TILE+32) ok; the second batch fails in the core */
	rdp_progressive_set_accel(&acc);
	rc[1] = rdp_progressive_decode(&ps[1], NULL, frame, len, px[1], W, H, NULL, NULL, &st[1], NULL);
	rdp_progressive_set_accel(NULL);
	inject_tile_fail_after = 0;
	for (i = 0; i < W * H; i++)
		diff += px[0][i] != px[1][i];
	CHECK(rc[0] == 0 && rc[1] == 0, "batch-fail decode rc %d / %d", rc[0], rc[1]);
	CHECK(tile_submits == 2 && tile_core_fails == fails_before + 1,
	      "batch-fail: submits %d, core fails %d", tile_submits, tile_core_fails - fails_before);
	CHECK(garbage_px > garbage_before, "batch-fail: the failed batch left no poison in px (test doesn't bite)");
	CHECK(st[1].cpu_fallback == 1 && st[1].accel_tiles == 0, "batch-fail: fallback %u accel_tiles %u",
	      (unsigned)st[1].cpu_fallback, (unsigned)st[1].accel_tiles);
	CHECK(diff == 0, "batch-fail: %d pixels differ from the CPU-only decode", diff);
	for (pass = 0; pass < 2; pass++)
	{
		rdp_progressive_free(&ps[pass], NULL);
		free(px[pass]);
	}
	printf("  follow-on batch fail (core NOMAP) -> CPU redo, pixel-exact: ok (poisoned px %ld)\n",
	       garbage_px - garbage_before);
}

/* >32 rectangles: more than one YCBCR batch, still pixel-exact vs CPU. */
static void test_many_rects(void)
{
	static uint8_t frame[400000];
	enum { GW = 2, GH = 2, W = 128, H = 128 };
	struct rdp_prog_surface ps[2];
	struct rdp_prog_stats st[2];
	struct rdp_prog_accel acc;
	uint32_t* px[2];
	int tx[1] = { 0 }, ty[1] = { 0 }, pass, i, diff = 0, before;
	size_t len;
	memset(&acc, 0, sizeof(acc));
	acc.rlgr = shim_rlgr;
	acc.tile = shim_tile;
	inject_tile_fail_after = 0;
	tile_submits = 0;
	before = tile_submits;
	rng = 7;
	len = make_region(frame, 1, 0, GW, GH, 1, tx, ty, 40);
	for (pass = 0; pass < 2; pass++)
	{
		memset(&ps[pass], 0, sizeof(ps[pass]));
		memset(&st[pass], 0, sizeof(st[pass]));
		px[pass] = calloc((size_t)W * H, 4);
		rdp_progressive_set_accel(pass ? &acc : NULL);
		CHECK(rdp_progressive_decode(&ps[pass], NULL, frame, len, px[pass], W, H, NULL, NULL, &st[pass], NULL) == 0,
		      "many-rects pass %d", pass);
	}
	rdp_progressive_set_accel(NULL);
	for (i = 0; i < W * H; i++)
		diff += px[0][i] != px[1][i];
	CHECK(diff == 0, "many-rects: %d pixels differ", diff);
	CHECK(st[1].cpu_fallback == 0 && st[1].accel_tiles >= 1, "many-rects accel/fallback");
	CHECK(tile_submits - before >= 2, "many-rects: need >1 batch, got %d", tile_submits - before);
	for (pass = 0; pass < 2; pass++)
	{
		rdp_progressive_free(&ps[pass], NULL);
		free(px[pass]);
	}
	printf("  >32 rects batched: ok (submits=%d)\n", tile_submits - before);
}

/* All stream rects outside the tile (tile in the view): progressive.c counts
 * empty_skip and never calls the hook, so no Submit and no accel_tiles. */
static void test_clipped_skip(void)
{
	static uint8_t frame[200000];
	enum { GW = 2, GH = 1, W = 128, H = 64 };
	struct rdp_prog_surface ps;
	struct rdp_prog_stats st;
	struct rdp_prog_accel acc;
	struct rdp_prog_clip clip = { 70, 0, 128, 64 }; /* only right tile visible */
	uint32_t* px = calloc(W * H, 4);
	int tx[1] = { 0 }, ty[1] = { 0 }; /* left tile: all rects after clip → n=0 */
	int before, rc;
	size_t len;
	uint8_t *p, *len_at, *tsize_at, *tiles, *t;
	memset(&ps, 0, sizeof(ps));
	memset(&st, 0, sizeof(st));
	memset(&acc, 0, sizeof(acc));
	acc.rlgr = shim_rlgr;
	acc.tile = shim_tile;
	inject_tile_fail_after = 0;
	/* The left tile is in the view clip, its one stream rect (x = 70) is not
	 * on it: n == 0 after clipping. */
	clip.x0 = 0; clip.y0 = 0; clip.x1 = 64; clip.y1 = 64; /* left tile in view */
	p = frame;
	p = put16(p, 0xCCC4);
	len_at = p; p += 4;
	*p++ = 64;
	p = put16(p, 1);   /* one rect entirely on the right tile */
	*p++ = 1; *p++ = 1; *p++ = 0;
	p = put16(p, 1);   /* one tile */
	tsize_at = p; p += 4;
	p = put16(p, 70); p = put16(p, 0); p = put16(p, 20); p = put16(p, 20); /* x=70 outside left */
	{
		int i;
		for (i = 0; i < 5; i++)
			*p++ = 0x66;
		*p++ = 0;
		for (i = 0; i < 15; i++)
			*p++ = 0;
	}
	tiles = p;
	t = p;
	p = put16(p, 0xCCC5);
	p += 4;
	*p++ = 0; *p++ = 0; *p++ = 0;
	p = put16(p, 0); p = put16(p, 0); /* left tile */
	*p++ = 0;
	p = put16(p, 300); p = put16(p, 300); p = put16(p, 300);
	p = put16(p, 0);
	p = rand_bytes(p, 300); p = rand_bytes(p, 300); p = rand_bytes(p, 300);
	put32(t + 2, (uint32_t)(p - t));
	put32(tsize_at, (uint32_t)(p - tiles));
	put32(len_at, (uint32_t)(p - frame));
	len = (size_t)(p - frame);
	(void)tx; (void)ty;
	before = calls_tile;
	tile_submits = 0;
	rdp_progressive_set_accel(&acc);
	rc = rdp_progressive_decode(&ps, NULL, frame, len, px, W, H, NULL, NULL, &st, &clip);
	rdp_progressive_set_accel(NULL);
	CHECK(rc == 0, "clipped-skip rc %d", rc);
	CHECK(calls_tile == before, "clipped-skip: tile hook must not run, ran %d", calls_tile - before);
	CHECK(tile_submits == 0, "clipped-skip: no Submit expected, got %d", tile_submits);
	CHECK(st.accel_tiles == 0 && st.cpu_fallback == 0 && st.empty_skip == 1,
	      "clipped-skip: accel_tiles %u fallback %u empty_skip %u", (unsigned)st.accel_tiles,
	      (unsigned)st.cpu_fallback, (unsigned)st.empty_skip);
	rdp_progressive_free(&ps, NULL);
	free(px);
	printf("  all-rects-clipped -> empty_skip, no hook, no Submit: ok\n");
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
	printf("failure / batch paths:\n");
	test_rlgr_truncated();
	test_rlgr_core_error();
	test_tile_batch_fail();
	test_many_rects();
	test_clipped_skip();
	return fails != 0;
}
