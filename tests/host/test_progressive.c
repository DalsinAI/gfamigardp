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
	if (!fails)
		bench_idwt(1, 200);
	printf(fails ? "FAILED %d\n" : "all ok\n", fails);
	return fails != 0;
}
