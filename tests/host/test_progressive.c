/* Host tests for the Progressive decode pieces. A test-only forward 5/3
 * DWT (lifting from [MS-RDPRFX] 3.1.8.1.4) checks the inverse.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "progressive.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void fwd1(const int32_t* xin, int xs, int n, int32_t* L, int ls, int32_t* H, int hs, int nl, int nh)
{
	int i, m = n;
	int32_t x[70], h[70];
	for (i = 0; i < n; i++)
		x[i] = xin[i * xs];
	if (nl + nh < 2 * nl && n == 64 && nl == 33)
	{
		x[64] = 2 * x[63] - x[62];   /* the 65th, extrapolated (3.2.8.1.2.2) */
		m = 65;
	}
	nh = m / 2;   /* 32 for 65 inputs; the last is dropped */
	for (i = 0; i < nh; i++)
	{
		int32_t a = x[2 * i], b = 2 * i + 2 < m ? x[2 * i + 2] : x[2 * i];
		h[i] = (x[2 * i + 1] - ((a + b) >> 1)) >> 1;
	}
	for (i = 0; i < nl; i++)
	{
		int32_t hp = h[i > 0 ? i - 1 : 0], hn = i < nh ? h[i] : h[nh - 1];
		L[i * ls] = x[2 * i] + ((hp + hn + 1) >> 1);
	}
	if (m == 65)
		nh = 31;
	for (i = 0; i < nh; i++)
		H[i * hs] = h[i];
}

/* one 2D level of an n x n block into LL/HL/LH/HH (packed). */
static void fwd2(const int32_t* x, int n, int nl, int nh, int32_t* LL, int32_t* HL, int32_t* LH, int32_t* HH)
{
	static int32_t lo[66 * 66], hi[66 * 66];
	int i;
	/* vertical first: columns */
	for (i = 0; i < n; i++)
		fwd1(x + i, n, n, lo + i, n, hi + i, n, nl, nh);
	/* horizontal on rows of lo (nl rows) and hi (nh rows) */
	for (i = 0; i < nl; i++)
		fwd1(lo + i * n, 1, n, LL + i * nl, 1, HL + i * nh, 1, nl, nh);
	for (i = 0; i < nh; i++)
		fwd1(hi + i * n, 1, n, LH + i * nl, 1, HH + i * nh, 1, nl, nh);
}

static int test_dwt(int re)
{
	static int32_t img[64 * 64], buf[4096], ll1[33 * 33], ll2[17 * 17], tmp[20000];
	int off[10], w, h, b, i, maxerr = 0, n1 = re ? 33 : 32, h1 = re ? 31 : 32, n2 = re ? 17 : 16, h2 = 16, n3 = re ? 9 : 8, h3 = 8;
	for (b = 0; b < 10; b++)
		rdp_prog_band_layout(re, b, &off[b], &w, &h);
	for (i = 0; i < 64 * 64; i++)
		img[i] = ((i % 64) * 3 + (i / 64) * 2) - 128 + ((i * 7919) % 5);
	fwd2(img, 64, n1, h1, ll1, buf + off[0], buf + off[1], buf + off[2]);
	fwd2(ll1, n1, n2, h2, ll2, buf + off[3], buf + off[4], buf + off[5]);
	fwd2(ll2, n2, n3, h3, buf + off[9], buf + off[6], buf + off[7], buf + off[8]);
	rdp_prog_idwt(buf, re, tmp);
	for (i = 0; i < 64 * 64; i++)
	{
		int e = abs(buf[i] - img[i]);
		if (e > maxerr) maxerr = e;
	}
	printf("dwt re=%d max error %d\n", re, maxerr);
	return maxerr;
}



static void on_paint_rect(void* user, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	int* n = user;
	(void)x0; (void)y0; (void)x1; (void)y1;
	(*n)++;
}

static int test_view_paint(void)
{
	/* Synthetic: empty stream does nothing; paint on empty surface is 0. */
	struct rdp_prog_surface ps;
	struct rdp_prog_stats st;
	struct rdp_prog_clip clip = { 0, 0, 128, 128 };
	uint32_t* px;
	int nrect = 0, rc;
	memset(&ps, 0, sizeof(ps));
	memset(&st, 0, sizeof(st));
	px = calloc(256 * 256, sizeof(uint32_t));
	CHECK(px);
	rc = rdp_progressive_decode(&ps, NULL, (const uint8_t*)"", 0, px, 256, 256,
	                            on_paint_rect, &nrect, &st, &clip);
	CHECK(rc == RDP_PROG_E_PROTO || rc == 0); /* empty is proto or ok depending on len check */
	/* short garbage */
	{
		uint8_t junk[8] = { 0 };
		rc = rdp_progressive_decode(&ps, NULL, junk, sizeof(junk), px, 256, 256,
		                            NULL, NULL, &st, &clip);
		CHECK(rc < 0 || rc == 0);
	}
	CHECK(rdp_progressive_paint(&ps, NULL, px, 256, 256, 0, 0, 64, 64, on_paint_rect, &nrect, &st) == 0);
	rdp_progressive_free(&ps, NULL);
	free(px);
	return 0;
}


static int test_trim(void)
{
	struct rdp_prog_surface ps;
	struct rdp_prog_stats st;
	struct rdp_prog_tile* t;
	memset(&ps, 0, sizeof(ps));
	memset(&st, 0, sizeof(st));
	ps.gw = 4; ps.gh = 4;
	ps.tiles = calloc((size_t)ps.gw * ps.gh, sizeof(*ps.tiles));
	CHECK(ps.tiles);
	t = calloc(1, sizeof(*t));
	CHECK(t);
	t->coef = calloc(3 * RDP_PROG_COEFS, sizeof(int16_t));
	CHECK(t->coef);
	t->painted = 1;
	ps.tiles[0] = t; /* tile (0,0) covers [0,64) */
	/* keep far away → free coefs, keep painted */
	CHECK(rdp_progressive_trim(&ps, NULL, 200, 200, 264, 264, &st) == 1);
	CHECK(t->coef == NULL);
	CHECK(t->painted == 1);
	CHECK(st.trimmed == 1);
	/* restore and keep near → keep coefs */
	t->coef = calloc(3 * RDP_PROG_COEFS, sizeof(int16_t));
	CHECK(t->coef);
	CHECK(rdp_progressive_trim(&ps, NULL, 0, 0, 64, 64, &st) == 0);
	CHECK(t->coef != NULL);
	rdp_progressive_free(&ps, NULL);
	return 0;
}

static uint8_t* put16(uint8_t* p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); return p + 2; }
static uint8_t* put32(uint8_t* p, uint32_t v) { p = put16(p, v & 0xFFFF); return put16(p, v >> 16); }

/* One region of one simple tile at (xi, yi), a whole-tile rect, three empty
 * components (RLGR reads zeros) and the tile flags given (test_prog_ogpu.c's
 * make_simple_tile with a place and flags). */
static size_t simple_tile(uint8_t* buf, int xi, int yi, int flags)
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
	p = put16(p, (unsigned)(xi * 64)); p = put16(p, (unsigned)(yi * 64)); p = put16(p, 64); p = put16(p, 64);
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
	p = put16(p, (unsigned)xi); p = put16(p, (unsigned)yi);
	*p++ = (uint8_t)flags;
	for (c = 0; c < 3; c++)
		p = put16(p, 0);
	p = put16(p, 0);
	put32(t + 2, (uint32_t)(p - t));
	put32(tsize_at, (uint32_t)(p - tiles));
	put32(len_at, (uint32_t)(p - buf));
	return (size_t)(p - buf);
}

/* A tile trim emptied, sent again: it gets new coefs (it used to come back
 * with coef NULL, and first_pass wrote 24 KB from address 0: on the Amiga,
 * the vectors; a freeze in the next interrupt, 6 Nov 2026). A difference
 * tile on it is skipped and leaves it trimmed. */
static int test_trimmed_tile_again(void)
{
	struct rdp_prog_surface ps;
	struct rdp_prog_stats st;
	struct rdp_prog_clip clip = { 0, 0, 64, 64 };
	uint8_t buf[256];
	uint32_t* px = calloc(256 * 256, sizeof(uint32_t));
	struct rdp_prog_tile* t;
	size_t n;
	memset(&ps, 0, sizeof(ps));
	memset(&st, 0, sizeof(st));
	CHECK(px);
	n = simple_tile(buf, 0, 0, 0);
	CHECK(rdp_progressive_decode(&ps, NULL, buf, n, px, 256, 256, NULL, NULL, &st, &clip) >= 0);
	CHECK(ps.tiles && (t = ps.tiles[0]) && t->coef);
	CHECK(rdp_progressive_trim(&ps, NULL, 128, 128, 256, 256, &st) == 1);
	CHECK(ps.tiles[0]->coef == NULL);
	n = simple_tile(buf, 0, 0, 1);   /* RFX_TILE_DIFFERENCE */
	CHECK(rdp_progressive_decode(&ps, NULL, buf, n, px, 256, 256, NULL, NULL, &st, &clip) >= 0);
	CHECK(ps.tiles[0]->coef == NULL);
	n = simple_tile(buf, 0, 0, 0);
	CHECK(rdp_progressive_decode(&ps, NULL, buf, n, px, 256, 256, NULL, NULL, &st, &clip) >= 0);
	CHECK(ps.tiles[0]->coef != NULL);
	rdp_progressive_free(&ps, NULL);
	free(px);
	return 0;
}

static void bench_idwt(int re, int rounds)
{
	static int32_t buf[4096], tmp[20000];
	int i, off, w, h, b;
	clock_t t0, t1;
	for (b = 0; b < 10; b++)
		rdp_prog_band_layout(re, b, &off, &w, &h);
	for (i = 0; i < 4096; i++)
		buf[i] = (int32_t)((i * 13) & 255) - 128;
	t0 = clock();
	for (i = 0; i < rounds; i++)
		rdp_prog_idwt(buf, re, tmp);
	t1 = clock();
	printf("bench idwt re=%d x%d: %.3f s (host clock)\n", re, rounds,
	       (double)(t1 - t0) / (double)CLOCKS_PER_SEC);
}

int main(void)
{
	int16_t out[4096];
	uint8_t zeros[64];
	int off, w, h, i, nz = 0;
	rdp_prog_band_layout(1, 9, &off, &w, &h);
	CHECK(off == 4015 && w == 9 && h == 9);
	rdp_prog_band_layout(1, 0, &off, &w, &h);
	CHECK(off == 0 && w == 31 && h == 33);
	rdp_prog_band_layout(1, 1, &off, &w, &h);
	CHECK(off == 1023 && w == 33 && h == 31);
	rdp_prog_band_layout(0, 9, &off, &w, &h);
	CHECK(off == 4032 && w == 8 && h == 8);
	memset(zeros, 0, sizeof(zeros));
	CHECK(rdp_rlgr1_decode(zeros, sizeof(zeros), out, 4096) == 0);
	for (i = 0; i < 4096; i++) nz += out[i] != 0;
	CHECK(nz == 0);
	CHECK(test_dwt(1) <= 8);
	CHECK(test_dwt(0) <= 8);
	test_view_paint();
	test_trim();
	test_trimmed_tile_again();
	if (!fails) {
		bench_idwt(1, 500);
		bench_idwt(0, 500);
	}
	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails != 0;
}
