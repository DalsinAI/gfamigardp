/*
 * GFAmigaRDP: RemoteFX Progressive decode. See progressive.h for the
 * sections each part follows. Written from the public specifications
 * only (CLEAN-ROOM.md). Plain C99.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include <string.h>
#include "progressive.h"

/* The compute back end (OpenGPU); zeroed: the CPU kernels below. */
static struct rdp_prog_accel s_accel;

void rdp_progressive_set_accel(const struct rdp_prog_accel* a)
{
	if (a)
		s_accel = *a;
	else
		memset(&s_accel, 0, sizeof(s_accel));
}


/* block types, 2.2.4.2.1 */
#define WBT_SYNC          0xCCC0
#define WBT_FRAME_BEGIN   0xCCC1
#define WBT_FRAME_END     0xCCC2
#define WBT_CONTEXT       0xCCC3
#define WBT_REGION        0xCCC4
#define WBT_TILE_SIMPLE   0xCCC5
#define WBT_TILE_FIRST    0xCCC6
#define WBT_TILE_UPGRADE  0xCCC7

#define RFX_SUBBAND_DIFFING         0x01
#define RFX_DWT_REDUCE_EXTRAPOLATE  0x01
#define RFX_TILE_DIFFERENCE         0x01

/* Bands in linearization order ([MS-RDPRFX] 3.1.8.1.6). */
enum { B_HL1, B_LH1, B_HH1, B_HL2, B_LH2, B_HH2, B_HL3, B_LH3, B_HH3, B_LL3, NBANDS };

/* RFX_COMPONENT_CODEC_QUANT (2.2.4.2.1.5.2) lists LL3, HL3, LH3, HH3, HL2,
 * LH2, HH2, HL1, LH1, HH1, low nibble first. */
static const uint8_t quant_band[NBANDS] = { B_LL3, B_HL3, B_LH3, B_HH3, B_HL2, B_LH2, B_HH2, B_HL1, B_LH1, B_HH1 };

static uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t u32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

/* Level sizes: low and high counts per level (1..3). Reduce-Extrapolate
 * (3.2.8.1.2.2): 33/31, 17/16, 9/8; the original method: 32/32, 16/16, 8/8. */
static void level_sizes(int re, int level, int* nl, int* nh)
{
	static const uint8_t lo_re[3] = { 33, 17, 9 }, hi_re[3] = { 31, 16, 8 };
	static const uint8_t lo[3] = { 32, 16, 8 }, hi[3] = { 32, 16, 8 };
	*nl = re ? lo_re[level - 1] : lo[level - 1];
	*nh = re ? hi_re[level - 1] : hi[level - 1];
}

/* HLk is high horizontally and low vertically (the top-right of [MS-RDPRFX]
 * figure 7): width nh, height nl. LHk: width nl, height nh. */
/* Precomputed once: band offset/w/h for both DWT layouts. Hot tiles call
 * this dozens of times; a table keeps the 68040 off the size loops. */
static struct { int16_t off, w, h; } s_band[2][NBANDS];
static uint8_t s_band_ready;

static void band_init(void)
{
	int re, band;
	if (s_band_ready)
		return;
	for (re = 0; re < 2; re++)
	{
		int o = 0;
		for (band = 0; band < NBANDS; band++)
		{
			int level = band == B_LL3 ? 3 : band / 3 + 1, nl, nh, bw, bh;
			level_sizes(re, level, &nl, &nh);
			switch (band == B_LL3 ? 3 : band % 3)
			{
			case 0: bw = nh; bh = nl; break;
			case 1: bw = nl; bh = nh; break;
			case 2: bw = nh; bh = nh; break;
			default: bw = nl; bh = nl; break;
			}
			s_band[re][band].off = (int16_t)o;
			s_band[re][band].w = (int16_t)bw;
			s_band[re][band].h = (int16_t)bh;
			o += bw * bh;
		}
	}
	s_band_ready = 1;
}

void rdp_prog_band_layout(int re, int band, int* off, int* w, int* h)
{
	band_init();
	re = re ? 1 : 0;
	if (band < 0 || band >= NBANDS)
	{
		*off = *w = *h = 0;
		return;
	}
	*off = s_band[re][band].off;
	*w = s_band[re][band].w;
	*h = s_band[re][band].h;
}

/* ---- bit reading, most significant bit first ([MS-RDPRFX] 3.1.8.1.7.3.1,
 * [MS-RDPEGFX] 3.2.8.1.5.2.1). A 32-bit accumulator avoids a shift per bit
 * on the 68040 hot path (RLGR / SRL). ---- */
struct bits
{
	const uint8_t* p;
	const uint8_t* end;
	uint32_t buf;     /* next bits in the high end */
	int nbuf;         /* how many bits are valid in buf */
	size_t pos;       /* bits consumed (for bits_over) */
	size_t lim;       /* len * 8 */
};

static void bits_init(struct bits* b, const uint8_t* p, size_t len)
{
	b->p = p;
	b->end = p + len;
	b->buf = 0;
	b->nbuf = 0;
	b->pos = 0;
	b->lim = len * 8;
}

static void bits_fill(struct bits* b)
{
	while (b->nbuf <= 24 && b->p < b->end)
	{
		b->buf |= (uint32_t)(*b->p++) << (24 - b->nbuf);
		b->nbuf += 8;
	}
}

static uint32_t get_bits(struct bits* b, int n)
{
	uint32_t v;
	if (n <= 0)
		return 0;
	if (b->nbuf < n)
		bits_fill(b);
	if (b->nbuf < n)
	{
		/* past the end: zeros, same as reading off the old bit stream */
		v = b->nbuf ? (b->buf >> (32 - b->nbuf)) << (n - b->nbuf) : 0;
		b->pos += (size_t)n;
		b->buf = 0;
		b->nbuf = 0;
		return v;
	}
	v = b->buf >> (32 - n);
	b->buf <<= n;
	b->nbuf -= n;
	b->pos += (size_t)n;
	return v;
}

static int bits_over(const struct bits* b)
{
	return b->pos > b->lim + 64;   /* far past the end: garbage */
}

/* ---- RLGR1 ([MS-RDPRFX] 3.1.8.1.7.3.1) ---- */
#define KPMAX 80
#define LSGR 3
#define UP_GR 4
#define DN_GR 6
#define UQ_GR 3
#define DQ_GR 3

static int update_param(int* param, int delta)
{
	*param += delta;
	if (*param > KPMAX) *param = KPMAX;
	if (*param < 0) *param = 0;
	return *param >> LSGR;
}

static uint32_t gr_code(struct bits* b, int* krp, int* kr)
{
	uint32_t vk = 0, mag;
	while (get_bits(b, 1) == 1)
	{
		vk++;
		if (bits_over(b))
			return 0;
	}
	mag = (vk << *kr) | get_bits(b, *kr);
	if (!vk)
		*kr = update_param(krp, -2);
	else if (vk != 1)
		*kr = update_param(krp, (int)vk);
	return mag;
}

int rdp_rlgr1_decode(const uint8_t* p, size_t len, int16_t* out, int n)
{
	struct bits b;
	int k = 1, kp = 1 << LSGR, kr = 1, krp = 1 << LSGR, o = 0;
	bits_init(&b, p, len);
	while (o < n)
	{
		if (bits_over(&b))
			return RDP_PROG_E_PROTO;
		if (k)
		{
			uint32_t run, sign;
			int mag;
			while (get_bits(&b, 1) == 0)
			{
				int z = 1 << k;
				if (z > n - o) z = n - o;
				if (z > 0)
				{
					memset(out + o, 0, (size_t)z * sizeof(int16_t));
					o += z;
				}
				k = update_param(&kp, UP_GR);
				if (o >= n || bits_over(&b))
					break;
			}
			if (o >= n)
				break;
			run = get_bits(&b, k);
			if (run > (uint32_t)(n - o))
				run = (uint32_t)(n - o);
			if (run)
			{
				memset(out + o, 0, (size_t)run * sizeof(int16_t));
				o += (int)run;
			}
			if (o >= n)
				break;
			sign = get_bits(&b, 1);
			mag = (int)gr_code(&b, &krp, &kr) + 1;
			out[o++] = (int16_t)(sign ? -mag : mag);
			k = update_param(&kp, -DN_GR);
		}
		else
		{
			uint32_t mag = gr_code(&b, &krp, &kr);   /* 2 * magnitude - sign */
			if (!mag)
			{
				out[o++] = 0;
				k = update_param(&kp, UQ_GR);
			}
			else
			{
				out[o++] = (int16_t)((mag & 1) ? -(int)((mag + 1) >> 1) : (int)(mag >> 1));
				k = update_param(&kp, -DQ_GR);
			}
		}
	}
	return 0;
}

/* ---- SRL ([MS-RDPEGFX] 3.1.8.1.5) ---- */
struct srl
{
	struct bits b;
	int kp, zeros, nonzero;   /* zeros still to give; a nonzero follows them */
};

static void srl_init(struct srl* s, const uint8_t* p, size_t len)
{
	bits_init(&s->b, p, len);
	s->kp = 8;
	s->zeros = 0;
	s->nonzero = 0;
}

static int srl_unary(struct srl* s, int nbits)
{
	int sign = (int)get_bits(&s->b, 1), mag = 1, max = (1 << nbits) - 1;
	while (mag < max && get_bits(&s->b, 1) == 0)
		mag++;
	return sign ? -mag : mag;
}

static int srl_next(struct srl* s, int nbits)
{
	if (s->zeros > 0)
	{
		s->zeros--;
		return 0;
	}
	if (s->nonzero)
	{
		s->nonzero = 0;
		return srl_unary(s, nbits);
	}
	{
		int k = s->kp >> 3, run = 0;
		while (get_bits(&s->b, 1) == 0)
		{
			run += 1 << k;
			s->kp += 4;
			if (s->kp > 80) s->kp = 80;
			k = s->kp >> 3;
			if (bits_over(&s->b))
				return 0;
		}
		run += (int)get_bits(&s->b, k);
		s->kp -= 6;
		if (s->kp < 0) s->kp = 0;
		if (run > 0)
		{
			s->zeros = run - 1;
			s->nonzero = 1;
			return 0;
		}
		return srl_unary(s, nbits);
	}
}

/* ---- inverse DWT ([MS-RDPRFX] 3.1.8.2.4, [MS-RDPEGFX] 3.3.8.2.2) ---- */

/* nl low, nh high (stride ls/hs between elements); the high band is
 * padded with zeros to nhe = nl - 1 or nl; nout of the nl + nhe outputs. */
static void idwt1(const int32_t* L, int ls, const int32_t* H, int hs, int nl, int nh, int nhe,
                  int32_t* X, int xs, int nout, int32_t* t)
{
	int n, total = nl + nhe;
	/* Even: L − ((H[n-1] + H[n] + 1) >> 1), H padded with 0 past nh. */
	for (n = 0; n < nl; n++)
	{
		int i0 = n > 0 ? n - 1 : 0, i1 = n < nhe ? n : (nhe > 0 ? nhe - 1 : 0);
		int32_t hp = i0 < nh ? H[i0 * hs] : 0;
		int32_t hn = i1 < nh ? H[i1 * hs] : 0;
		t[2 * n] = L[n * ls] - ((hp + hn + 1) >> 1);
	}
	for (n = 0; n < nhe; n++)
	{
		int32_t a = t[2 * n], b = 2 * n + 2 < total ? t[2 * n + 2] : t[2 * n];
		int32_t hn = n < nh ? H[n * hs] : 0;
		t[2 * n + 1] = 2 * hn + ((a + b) >> 1);
	}
	/* Contiguous output (horizontal pass): one memcpy beats n stores. */
	if (xs == 1)
		memcpy(X, t, (size_t)nout * sizeof(int32_t));
	else
		for (n = 0; n < nout; n++)
			X[n * xs] = t[n];
}

/* One 2D level: LL (a x a, stride las), HL (b wide, a high), LH (a wide,
 * b high), HH (b x b), all packed; out n x n (stride n), n = a + b, or 64
 * for the Reduce-Extrapolate first level. */
static void idwt2(const int32_t* LL, int las, const int32_t* HL, const int32_t* LH, const int32_t* HH,
                  int a, int b, int be, int n, int32_t* out, int32_t* lp, int32_t* hp, int32_t* t)
{
	int y, x;
	/* horizontal: rows of LL with HL, rows of LH with HH */
	for (y = 0; y < a; y++)
		idwt1(LL + y * las, 1, HL + y * b, 1, a, b, be, lp + y * n, 1, n, t);
	for (y = 0; y < b; y++)
		idwt1(LH + y * a, 1, HH + y * b, 1, a, b, be, hp + y * n, 1, n, t);
	/* vertical */
	for (x = 0; x < n; x++)
		idwt1(lp + x, n, hp + x, n, a, b, be, out + x, n, n, t);
}

/* buf: 4096 dequantized coefficients in band order; out: the 64 x 64
 * component written back into buf. tmp: RDP_PROG_TMP ints. */
void rdp_prog_idwt(int32_t* buf, int re, int32_t* tmp)
{
	/* tmp layout: ll (33*33) | lp (33*64) | hp (33*64) | t (2*66) | mid (64*64).
	 * mid holds the level-3/2 intermediate; the final 64x64 lands in buf. */
	int32_t *ll = tmp, *lp = ll + 33 * 33, *hp = lp + 33 * 64, *t = hp + 33 * 64, *mid = t + 2 * 66;
	int off[NBANDS], i, l3, h3, l2, h2, l1, h1, n2, n1, rr;
	band_init();
	rr = re ? 1 : 0;
	for (i = 0; i < NBANDS; i++)
		off[i] = s_band[rr][i].off;
	level_sizes(re, 3, &l3, &h3);
	level_sizes(re, 2, &l2, &h2);
	level_sizes(re, 1, &l1, &h1);
	n2 = l3 + h3;   /* 17 or 16 = l2 */
	n1 = l2 + h2;   /* 33 or 32 = l1 */
	/* level 3 -> LL2 in mid */
	idwt2(buf + off[B_LL3], l3, buf + off[B_HL3], buf + off[B_LH3], buf + off[B_HH3], l3, h3,
	      re ? l3 - 1 : l3, n2, mid, lp, hp, t);
	/* level 2 -> LL1 in ll */
	idwt2(mid, n2, buf + off[B_HL2], buf + off[B_LH2], buf + off[B_HH2], l2, h2,
	      re ? l2 - 1 : l2, n1, ll, lp, hp, t);
	/* level 1 -> 64 x 64 written straight into buf. Horizontal has already
	 * copied HL1/LH1/HH1 into lp/hp, so overwriting the band layout is safe
	 * and saves a 16 KiB memcpy per component (48 KiB per tile). */
	idwt2(ll, n1, buf + off[B_HL1], buf + off[B_LH1], buf + off[B_HH1], l1, h1,
	      re ? l1 - 1 : l1, 64, buf, lp, hp, t);
}

/* ---- tiles ---- */

struct region
{
	const uint8_t* quant;      /* numQuant x 5 */
	const uint8_t* prog;       /* numProgQuant x 16 */
	int nquant, nprog, re;
};

struct ctx
{
	struct rdp_prog_surface* ps;
	const struct rdp_mem* mem;
	uint32_t* px;
	uint16_t w, h;
	rdp_prog_rect_fn on_rect;
	void* user;
	struct rdp_prog_stats* st;
	uint8_t diffing;
	const uint8_t* rects;      /* every TS_RFX_RECT of the stream, 8 bytes each */
	int nrects;
	struct rdp_prog_tile scratch;
	int16_t* vals;             /* 3 x 4096: one component each */
	uint16_t* clipped;         /* nrects x 4: render()'s rectangles for the accel */
	int32_t* work;             /* 4096 */
	int32_t* tmp;              /* RDP_PROG_TMP */
	int32_t* ycc;              /* 3 x 4096 */
	uint8_t have_clip;
	uint8_t revived;               /* tile_state gave a trimmed tile new (zero) coefs */
	uint16_t cx0, cy0, cx1, cy1;   /* exclusive view clip in surface coords */
};

static void unpack_quant(const uint8_t* q5, uint8_t out[NBANDS])
{
	int i;
	for (i = 0; i < NBANDS; i++)
	{
		uint8_t v = q5[i >> 1];
		out[quant_band[i]] = (uint8_t)((i & 1) ? v >> 4 : v & 0x0F);
	}
}

static struct rdp_prog_tile* tile_state(struct ctx* c, uint16_t xi, uint16_t yi, int make, int want_sign)
{
	struct rdp_prog_surface* ps = c->ps;
	struct rdp_prog_tile** slot;
	if (!ps->tiles)
	{
		size_t n;
		if (!make)
			return NULL;
		ps->gw = (uint16_t)((c->w + 63) / 64);
		ps->gh = (uint16_t)((c->h + 63) / 64);
		n = (size_t)ps->gw * ps->gh * sizeof(*ps->tiles);
		if (!(ps->tiles = (struct rdp_prog_tile**)rdp_mem_alloc(c->mem, n)))
			return NULL;
		memset(ps->tiles, 0, n);
	}
	if (xi >= ps->gw || yi >= ps->gh)
		return NULL;
	slot = &ps->tiles[(size_t)yi * ps->gw + xi];
	c->revived = 0;
	/* A tile rdp_progressive_trim emptied (off the view, coefs freed, pixels
	 * kept) has no coefs: without make it isn't usable; with make it gets new
	 * zero coefs. Before, it came back with coef NULL and first_pass wrote
	 * 3 x 4096 int16 through it: address 0 up, the 68k's vectors and exec's
	 * low memory, and the Amiga froze in its next interrupt (6 Nov 2026,
	 * GFAmigaRDP against GNOME Remote Desktop, after a pan or the first view). */
	if (*slot && !(*slot)->coef)
	{
		if (!make)
			return NULL;
		if (!((*slot)->coef = (int16_t*)rdp_mem_alloc(c->mem, 3 * RDP_PROG_COEFS * sizeof(int16_t))))
			return NULL;
		memset((*slot)->coef, 0, 3 * RDP_PROG_COEFS * sizeof(int16_t));
		c->revived = 1;
	}
	if (!*slot && make)
	{
		struct rdp_prog_tile* t = (struct rdp_prog_tile*)rdp_mem_alloc(c->mem, sizeof(*t));
		if (!t)
			return NULL;
		memset(t, 0, sizeof(*t));
		if (!(t->coef = (int16_t*)rdp_mem_alloc(c->mem, 3 * RDP_PROG_COEFS * sizeof(int16_t))))
		{
			rdp_mem_free(c->mem, t);
			return NULL;
		}
		memset(t->coef, 0, 3 * RDP_PROG_COEFS * sizeof(int16_t));
		*slot = t;
	}
	if (*slot && want_sign && !(*slot)->sign)
	{
		if (!((*slot)->sign = (int8_t*)rdp_mem_alloc(c->mem, 3 * RDP_PROG_COEFS)))
			return NULL;
		memset((*slot)->sign, 0, 3 * RDP_PROG_COEFS);
	}
	return *slot;
}

static int tile_in_clip(const struct ctx* c, uint16_t xi, uint16_t yi)
{
	int32_t tx = (int32_t)xi * 64, ty = (int32_t)yi * 64, tx1 = tx + 64, ty1 = ty + 64;
	if (!c->have_clip)
		return 1;
	return !(tx1 <= c->cx0 || ty1 <= c->cy0 || tx >= c->cx1 || ty >= c->cy1);
}

/* Dequantize, inverse DWT, colour convert and write the tile, clipped to
 * the stream's rectangles and the surface. Skips the heavy path when the
 * whole tile lies outside the view clip (coefs stay; paint brings them
 * back). Stream rects are not further clipped to the view: a half-drawn
 * edge tile with painted set made pan/paint skip the rest. */
static void render(struct ctx* c, struct rdp_prog_tile* t, uint16_t xi, uint16_t yi)
{
	int comp, b, i, r, n = 0;
	int32_t tx = (int32_t)xi * 64, ty = (int32_t)yi * 64;
	const int re = t->re ? 1 : 0;

	/* View clip is tile-level only (STATUS-pan-opt): a tile outside the pan
	 * band skips the heavy path (coefs stay; paint brings them back). Do not
	 * further clip stream rects to the view — that left edge tiles half-drawn
	 * with painted set, so pan/paint skipped the rest (stale pixels). */
	if (!tile_in_clip(c, xi, yi))
	{
		c->st->view_skip++;
		return; /* painted stays 0 */
	}
	/* Stream rectangles clipped to the tile and the surface only. */
	for (r = 0; r < c->nrects; r++)
	{
		const uint8_t* q = c->rects + r * 8;
		int32_t x0 = u16(q), y0 = u16(q + 2), x1 = x0 + u16(q + 4), y1 = y0 + u16(q + 6);
		uint16_t* o = c->clipped + n * 4;
		if (x0 < tx) x0 = tx;
		if (y0 < ty) y0 = ty;
		if (x1 > tx + 64) x1 = tx + 64;
		if (y1 > ty + 64) y1 = ty + 64;
		if (x1 > c->w) x1 = c->w;
		if (y1 > c->h) y1 = c->h;
		if (x0 >= x1 || y0 >= y1)
			continue;
		o[0] = (uint16_t)x0; o[1] = (uint16_t)y0; o[2] = (uint16_t)x1; o[3] = (uint16_t)y1;
		n++;
	}
	/* Nothing of the tile to paint (stream rects miss this tile): skip the
	 * dequant / DWT / YCbCr (coefs stay) and don't ask the accel. */
	if (n == 0)
	{
		c->st->empty_skip++;
		goto done;
	}
	/* OpenGPU: dequantize, DWT and YCbCr of the whole tile in one batch */
	if (s_accel.tile)
	{
		if (s_accel.tile(s_accel.user, t->coef, (const uint8_t(*)[10])t->quant, re, c->ycc, c->tmp,
		                 c->px, c->w, (int)tx, (int)ty, c->clipped, n))
		{
			c->st->accel_tiles++;
			goto done;
		}
		c->st->cpu_fallback++;
	}
	band_init();
	for (comp = 0; comp < 3; comp++)
	{
		const int16_t* co = t->coef + comp * RDP_PROG_COEFS;
		int32_t* w = c->ycc + comp * RDP_PROG_COEFS;
		for (b = 0; b < NBANDS; b++)
		{
			int sh = (int)t->quant[comp][b] - 6;
			int off = s_band[re][b].off, nb = (int)s_band[re][b].w * s_band[re][b].h;
			if (sh >= 0)
			{
				int32_t m = 1 << sh;
				for (i = off; i < off + nb; i++)
					w[i] = (int32_t)co[i] * m;
			}
			else
			{
				int rs = -sh;
				for (i = off; i < off + nb; i++)
					w[i] = (int32_t)co[i] >> rs;
			}
		}
		rdp_prog_idwt(w, re, c->tmp);
	}
	for (r = 0; r < n; r++)
	{
		const uint16_t* q = c->clipped + r * 4;
		int32_t x0 = q[0], y0 = q[1], x1 = q[2], y1 = q[3], y;
		/* [MS-RDPRFX] 3.1.8.2.5: Y + 128; matrix in 16.16. Hot path for 68040.
		 * The products wrap in 32 bits (unsigned), as the 68040's muls.l
		 * does, so a garbage tile is defined C and matches OpenGPU's
		 * RFX_YCBCR bit for bit. */
		for (y = y0; y < y1; y++)
		{
			uint32_t* dst = c->px + (size_t)y * c->w + (size_t)x0;
			const int32_t* Y = c->ycc + (y - ty) * 64 + (x0 - tx);
			const int32_t* Cb = Y + RDP_PROG_COEFS;
			const int32_t* Cr = Cb + RDP_PROG_COEFS;
			int nx = (int)(x1 - x0), k;
			for (k = 0; k < nx; k++)
			{
				int32_t yy = Y[k] + 128;
				uint32_t cb = (uint32_t)Cb[k], cr = (uint32_t)Cr[k];
				int32_t R = yy + ((int32_t)(cr * 91916u + 32768u) >> 16);
				int32_t G = yy - ((int32_t)(cb * 22527u + cr * 46819u + 32768u) >> 16);
				int32_t B = yy + ((int32_t)(cb * 115993u + 32768u) >> 16);
				uint32_t rr = (uint32_t)R, g = (uint32_t)G, bb = (uint32_t)B;
				if (rr > 255u) rr = (uint32_t)(R < 0 ? 0 : 255);
				if (g > 255u) g = (uint32_t)(G < 0 ? 0 : 255);
				if (bb > 255u) bb = (uint32_t)(B < 0 ? 0 : 255);
				dst[k] = 0xFF000000u | (rr << 16) | (g << 8) | bb;
			}
		}
	}
done:
	if (c->on_rect)
		for (r = 0; r < n; r++)
		{
			const uint16_t* q = c->clipped + r * 4;
			c->on_rect(c->user, q[0], q[1], q[2], q[3]);
		}
	t->painted = 1;
}

static int first_pass(struct ctx* c, const struct region* rg, const uint8_t* p, size_t len, int simple)
{
	size_t hdr = simple ? 22 : 23, pos;
	uint8_t qi[3], flags, quality = 0xFF;
	uint16_t xi, yi, clen[3];
	struct rdp_prog_tile* t;
	int comp, b, i, diff, progressive, off, bw, bh;
	if (len < hdr)
		return RDP_PROG_E_PROTO;
	qi[0] = p[6];
	qi[1] = p[7];
	qi[2] = p[8];
	xi = u16(p + 9);
	yi = u16(p + 11);
	flags = p[13];
	if (!simple)
		quality = p[14];
	pos = simple ? 14 : 15;
	clen[0] = u16(p + pos);
	clen[1] = u16(p + pos + 2);
	clen[2] = u16(p + pos + 4);
	/* tailLen at pos + 6 is ignored */
	if (hdr + (size_t)clen[0] + clen[1] + clen[2] > len)
		return RDP_PROG_E_PROTO;
	for (i = 0; i < 3; i++)
		if (qi[i] >= rg->nquant)
			return RDP_PROG_E_PROTO;
	if (quality != 0xFF && quality >= rg->nprog)
		return RDP_PROG_E_PROTO;
	diff = (flags & RFX_TILE_DIFFERENCE) != 0;
	progressive = quality != 0xFF;
	/* Keep state when a view clip is set so off-screen tiles can be painted
	 * after a pan without waiting for the server to send them again. */
	t = tile_state(c, xi, yi, c->diffing || progressive || diff || c->have_clip, progressive);
	if (t && diff && c->revived)
	{
		/* a difference on coefs trim dropped can't be applied: back to the
		 * trimmed state (its pixels stay); the next full tile repairs it */
		rdp_mem_free(c->mem, t->coef);
		t->coef = NULL;
		if (t->sign)
		{
			rdp_mem_free(c->mem, t->sign);
			t->sign = NULL;
		}
		c->st->skipped++;
		return 0;
	}
	if (!t)
	{
		if (diff || progressive || c->have_clip || xi >= (c->w + 63) / 64 || yi >= (c->h + 63) / 64)
		{
			c->st->nomem += (xi < (c->w + 63) / 64 && yi < (c->h + 63) / 64);
			c->st->skipped++;
			return 0;   /* nowhere to keep it; the next full tile repairs this */
		}
		t = &c->scratch;   /* a full, final, original tile needs no state */
	}
	/* RLGR1 of the three components (and LL3's deltas, summed even for a
	 * difference tile, 3.3.8.2.1.1): OpenGPU in one batch, else here. Done
	 * before the tile changes, so a malformed component leaves it as it was. */
	{
		const uint8_t* src[3];
		int got = 0;
		src[0] = p + hdr;
		src[1] = src[0] + clen[0];
		src[2] = src[1] + clen[1];
		if (s_accel.rlgr)
		{
			got = s_accel.rlgr(s_accel.user, src, clen, rg->re, c->vals);
			if (got < 0)
				return RDP_PROG_E_PROTO;
			if (got)
				c->st->accel_rlgr++;
			else
				c->st->cpu_fallback++;
		}
		if (!got)
			for (comp = 0; comp < 3; comp++)
			{
				int16_t* v = c->vals + comp * RDP_PROG_COEFS;
				if (rdp_rlgr1_decode(src[comp], clen[comp], v, RDP_PROG_COEFS) < 0)
					return RDP_PROG_E_PROTO;
				rdp_prog_band_layout(rg->re, B_LL3, &off, &bw, &bh);
				for (i = off + 1; i < off + bw * bh; i++)
					v[i] = (int16_t)(v[i] + v[i - 1]);
			}
	}
	t->painted = 0;
	t->re = (uint8_t)rg->re;
	pos = hdr;
	for (comp = 0; comp < 3; comp++)
	{
		int16_t* co = t->coef + comp * RDP_PROG_COEFS;
		int8_t* sg = t->sign ? t->sign + comp * RDP_PROG_COEFS : NULL;
		const int16_t* vals = c->vals + comp * RDP_PROG_COEFS;
		uint8_t bp[NBANDS];
		pos += clen[comp];
		unpack_quant(rg->quant + qi[comp] * 5, t->quant[comp]);
		if (progressive)
			unpack_quant(rg->prog + quality * 16 + 1 + comp * 5, bp);
		else
			memset(bp, 0, sizeof(bp));
		for (b = 0; b < NBANDS; b++)
		{
			int32_t m = 1 << bp[b];
			rdp_prog_band_layout(rg->re, b, &off, &bw, &bh);
			for (i = off; i < off + bw * bh; i++)
			{
				int32_t v = vals[i];
				co[i] = (int16_t)(diff ? co[i] + v * m : v * m);
				if (sg)
					sg[i] = (int8_t)(b == B_LL3 ? 0 : v > 0 ? 1 : v < 0 ? -1 : 0);
			}
			t->bitpos[comp][b] = bp[b];
		}
	}
	if (simple)
		c->st->simple++;
	else
		c->st->first++;
	render(c, t, xi, yi);
	return 0;
}

static int upgrade_pass(struct ctx* c, const struct region* rg, const uint8_t* p, size_t len)
{
	uint8_t qi[3], quality;
	uint16_t xi, yi, sl[3], rl[3];
	size_t pos = 26;
	struct rdp_prog_tile* t;
	int comp, b, i, off, bw, bh;
	if (len < 26)
		return RDP_PROG_E_PROTO;
	qi[0] = p[6];
	qi[1] = p[7];
	qi[2] = p[8];
	xi = u16(p + 9);
	yi = u16(p + 11);
	quality = p[13];
	for (i = 0; i < 3; i++)
	{
		sl[i] = u16(p + 14 + i * 4);
		rl[i] = u16(p + 16 + i * 4);
		if (qi[i] >= rg->nquant)
			return RDP_PROG_E_PROTO;
	}
	if (26 + (size_t)sl[0] + rl[0] + sl[1] + rl[1] + sl[2] + rl[2] > len)
		return RDP_PROG_E_PROTO;
	if (quality != 0xFF && quality >= rg->nprog)
		return RDP_PROG_E_PROTO;
	t = tile_state(c, xi, yi, 0, 0);
	if (!t || !t->sign)
	{
		c->st->skipped++;   /* no first pass seen */
		return 0;
	}
	for (comp = 0; comp < 3; comp++)
	{
		int16_t* co = t->coef + comp * RDP_PROG_COEFS;
		int8_t* sg = t->sign + comp * RDP_PROG_COEFS;
		uint8_t bp[NBANDS];
		struct srl srl;
		struct bits raw;
		srl_init(&srl, p + pos, sl[comp]);
		pos += sl[comp];
		bits_init(&raw, p + pos, rl[comp]);
		pos += rl[comp];
		unpack_quant(rg->quant + qi[comp] * 5, t->quant[comp]);
		if (quality != 0xFF)
			unpack_quant(rg->prog + quality * 16 + 1 + comp * 5, bp);
		else
			memset(bp, 0, sizeof(bp));
		for (b = 0; b < NBANDS; b++)
		{
			int nb = (int)t->bitpos[comp][b] - bp[b];
			int32_t m = 1 << bp[b];
			if (nb <= 0)
				continue;   /* nothing for this band in this pass */
			rdp_prog_band_layout(t->re, b, &off, &bw, &bh);
			for (i = off; i < off + bw * bh; i++)
			{
				int32_t in;
				if (b == B_LL3 || sg[i] > 0)
					co[i] = (int16_t)(co[i] + (int32_t)get_bits(&raw, nb) * m);
				else if (sg[i] < 0)
					co[i] = (int16_t)(co[i] - (int32_t)get_bits(&raw, nb) * m);
				else
				{
					in = srl_next(&srl, nb);
					co[i] = (int16_t)(co[i] + in * m);
					sg[i] = (int8_t)(in > 0 ? 1 : in < 0 ? -1 : 0);
				}
			}
			t->bitpos[comp][b] = bp[b];
		}
	}
	c->st->upgrade++;
	t->painted = 0;   /* coefs changed; view-skip must repaint later */
	render(c, t, xi, yi);
	return 0;
}

static int region(struct ctx* c, const uint8_t* p, size_t len)
{
	struct region rg;
	size_t pos = 18, end;
	uint16_t nrects, ntiles, k;
	uint32_t tsize;
	if (len < 18)
		return RDP_PROG_E_PROTO;
	nrects = u16(p + 7);
	rg.nquant = p[9];
	rg.nprog = p[10];
	rg.re = (p[11] & RFX_DWT_REDUCE_EXTRAPOLATE) != 0;
	ntiles = u16(p + 12);
	tsize = u32(p + 14);
	pos += (size_t)nrects * 8;
	rg.quant = p + pos;
	pos += (size_t)rg.nquant * 5;
	rg.prog = p + pos;
	pos += (size_t)rg.nprog * 16;
	if (pos > len || tsize > len - pos)
		return RDP_PROG_E_PROTO;
	end = pos + tsize;
	for (k = 0; k < ntiles && pos + 6 <= end; k++)
	{
		uint16_t type = u16(p + pos);
		uint32_t bl = u32(p + pos + 2);
		int rc = 0;
		if (bl < 6 || bl > end - pos)
			return RDP_PROG_E_PROTO;
		if (type == WBT_TILE_SIMPLE)
			rc = first_pass(c, &rg, p + pos, bl, 1);
		else if (type == WBT_TILE_FIRST)
			rc = first_pass(c, &rg, p + pos, bl, 0);
		else if (type == WBT_TILE_UPGRADE)
			rc = upgrade_pass(c, &rg, p + pos, bl);
		if (rc < 0)
			return rc;
		pos += bl;
	}
	return 0;
}

int rdp_progressive_decode(struct rdp_prog_surface* ps, const struct rdp_mem* mem,
                           const uint8_t* data, size_t len, uint32_t* px, uint16_t w, uint16_t h,
                           rdp_prog_rect_fn on_rect, void* user, struct rdp_prog_stats* st,
                           const struct rdp_prog_clip* clip)
{
	struct ctx c;
	struct rdp_prog_stats dummy;
	size_t pos, nr = 0, bytes;
	uint8_t* rects = NULL;
	int rc = 0;
	memset(&c, 0, sizeof(c));
	if (!st)
		st = &dummy;
	if (clip && clip->x1 > clip->x0 && clip->y1 > clip->y0)
	{
		c.have_clip = 1;
		c.cx0 = clip->x0;
		c.cy0 = clip->y0;
		c.cx1 = clip->x1 > w ? w : clip->x1;
		c.cy1 = clip->y1 > h ? h : clip->y1;
	}
	/* pass 1: the rectangles of every region (they may share tiles, 2.2.4.2.1.5) */
	for (pos = 0; pos + 6 <= len;)
	{
		uint32_t bl = u32(data + pos + 2);
		if (bl < 6 || bl > len - pos)
			return RDP_PROG_E_PROTO;
		if (u16(data + pos) == WBT_REGION && bl >= 18)
			nr += u16(data + pos + 7);
		pos += u16(data + pos) == WBT_FRAME_BEGIN ? 12 : bl;
	}
	/* rects (nr x 8) | clipped (nr x 8) | work | tmp | ycc (3) | vals (3) | scratch coef (3) */
	bytes = nr * 16 + 4 + RDP_PROG_COEFS * sizeof(int32_t) + RDP_PROG_TMP * sizeof(int32_t) +
	        3 * RDP_PROG_COEFS * sizeof(int32_t) + 6 * RDP_PROG_COEFS * sizeof(int16_t);
	if (!(rects = (uint8_t*)rdp_mem_alloc(mem, bytes)))
		return RDP_PROG_E_NOMEM;
	c.ps = ps;
	c.mem = mem;
	c.px = px;
	c.w = w;
	c.h = h;
	c.on_rect = on_rect;
	c.user = user;
	c.st = st;
	c.diffing = ps->diffing;
	c.rects = rects;
	c.clipped = (uint16_t*)(void*)(rects + nr * 8);
	c.work = (int32_t*)(void*)(rects + ((nr * 16 + 3) & ~(size_t)3));
	c.tmp = c.work + RDP_PROG_COEFS;
	c.ycc = c.tmp + RDP_PROG_TMP;
	c.vals = (int16_t*)(void*)(c.ycc + 3 * RDP_PROG_COEFS);
	c.scratch.coef = c.vals + 3 * RDP_PROG_COEFS;
	for (pos = 0; pos + 6 <= len;)
	{
		uint32_t bl = u32(data + pos + 2);
		if (u16(data + pos) == WBT_REGION && bl >= 18)
		{
			uint16_t n = u16(data + pos + 7);
			if (18 + (size_t)n * 8 <= bl)
			{
				memcpy(rects + (size_t)c.nrects * 8, data + pos + 18, (size_t)n * 8);
				c.nrects += n;
			}
		}
		pos += u16(data + pos) == WBT_FRAME_BEGIN ? 12 : bl;
	}
	/* pass 2: decode */
	for (pos = 0; pos + 6 <= len && rc >= 0;)
	{
		uint16_t type = u16(data + pos);
		uint32_t bl = u32(data + pos + 2);
		switch (type)
		{
		case WBT_CONTEXT:
			if (bl >= 10)
				ps->diffing = c.diffing = (data[pos + 9] & RFX_SUBBAND_DIFFING) != 0;
			break;
		case WBT_REGION:
			rc = region(&c, data + pos, bl);
			break;
		case WBT_FRAME_BEGIN:
			bl = 12;   /* its regions follow as blocks of their own */
			break;
		default:
			break;     /* SYNC, FRAME_END, unknown: nothing to do */
		}
		pos += bl;
	}
	rdp_mem_free(mem, rects);
	return rc;
}

int rdp_progressive_paint(struct rdp_prog_surface* ps, const struct rdp_mem* mem,
                          uint32_t* px, uint16_t w, uint16_t h,
                          uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                          rdp_prog_rect_fn on_rect, void* user, struct rdp_prog_stats* st)
{
	struct ctx c;
	struct rdp_prog_stats dummy;
	uint8_t rect[8];
	uint16_t clipped[4];
	size_t bytes;
	uint8_t* buf;
	uint16_t xi0, yi0, xi1, yi1, xi, yi;
	int n = 0;

	if (!ps || !ps->tiles || !px || x0 >= x1 || y0 >= y1)
		return 0;
	if (x1 > w) x1 = w;
	if (y1 > h) y1 = h;
	if (!st)
		st = &dummy;
	memset(&c, 0, sizeof(c));
	bytes = RDP_PROG_COEFS * sizeof(int32_t) + RDP_PROG_TMP * sizeof(int32_t) +
	        3 * RDP_PROG_COEFS * sizeof(int32_t);
	if (!(buf = (uint8_t*)rdp_mem_alloc(mem, bytes)))
		return RDP_PROG_E_NOMEM;
	c.ps = ps;
	c.mem = mem;
	c.px = px;
	c.w = w;
	c.h = h;
	c.on_rect = on_rect;
	c.user = user;
	c.st = st;
	c.have_clip = 1;
	c.cx0 = x0;
	c.cy0 = y0;
	c.cx1 = x1;
	c.cy1 = y1;
	c.work = (int32_t*)(void*)buf;
	c.tmp = c.work + RDP_PROG_COEFS;
	c.ycc = c.tmp + RDP_PROG_TMP;
	/* one TS_RFX_RECT covering the paint window (x, y, width, height) */
	rect[0] = (uint8_t)(x0); rect[1] = (uint8_t)(x0 >> 8);
	rect[2] = (uint8_t)(y0); rect[3] = (uint8_t)(y0 >> 8);
	{
		uint16_t rw = (uint16_t)(x1 - x0), rh = (uint16_t)(y1 - y0);
		rect[4] = (uint8_t)rw; rect[5] = (uint8_t)(rw >> 8);
		rect[6] = (uint8_t)rh; rect[7] = (uint8_t)(rh >> 8);
	}
	c.rects = rect;
	c.nrects = 1;
	c.clipped = clipped;
	xi0 = (uint16_t)(x0 / 64);
	yi0 = (uint16_t)(y0 / 64);
	xi1 = (uint16_t)((x1 + 63) / 64);
	yi1 = (uint16_t)((y1 + 63) / 64);
	if (xi1 > ps->gw) xi1 = ps->gw;
	if (yi1 > ps->gh) yi1 = ps->gh;
	for (yi = yi0; yi < yi1; yi++)
		for (xi = xi0; xi < xi1; xi++)
		{
			struct rdp_prog_tile* tile = ps->tiles[(size_t)yi * ps->gw + xi];
			if (!tile || !tile->coef || tile->painted)
				continue;
			render(&c, tile, xi, yi);
			if (tile->painted)
			{
				st->painted++;
				n++;
				/* render writes only the part inside the paint window. A tile
				 * the window doesn't wholly cover (within the surface) stays
				 * unpainted, so a later paint of the rest renders it again;
				 * marking it painted left the rest stale. */
				if ((int32_t)xi * 64 < (int32_t)x0 || (int32_t)yi * 64 < (int32_t)y0 ||
				    ((int32_t)xi * 64 + 64 < (int32_t)w ? (int32_t)xi * 64 + 64 : (int32_t)w) > (int32_t)x1 ||
				    ((int32_t)yi * 64 + 64 < (int32_t)h ? (int32_t)yi * 64 + 64 : (int32_t)h) > (int32_t)y1)
					tile->painted = 0;
			}
		}
	rdp_mem_free(mem, buf);
	return n;
}


int rdp_progressive_trim(struct rdp_prog_surface* ps, const struct rdp_mem* mem,
                         uint16_t keep_x0, uint16_t keep_y0, uint16_t keep_x1, uint16_t keep_y1,
                         struct rdp_prog_stats* st)
{
	uint16_t xi, yi, n = 0;
	struct rdp_prog_stats dummy;
	if (!ps || !ps->tiles || keep_x1 <= keep_x0 || keep_y1 <= keep_y0)
		return 0;
	if (!st)
		st = &dummy;
	for (yi = 0; yi < ps->gh; yi++)
		for (xi = 0; xi < ps->gw; xi++)
		{
			struct rdp_prog_tile* t = ps->tiles[(size_t)yi * ps->gw + xi];
			int32_t tx = (int32_t)xi * 64, ty = (int32_t)yi * 64;
			if (!t || !t->coef)
				continue;
			/* Completely outside the keep rect: drop coefs (pixels stay if painted). */
			if (!(tx + 64 <= keep_x0 || ty + 64 <= keep_y0 || tx >= keep_x1 || ty >= keep_y1))
				continue;
			rdp_mem_free(mem, t->coef);
			t->coef = NULL;
			if (t->sign)
			{
				rdp_mem_free(mem, t->sign);
				t->sign = NULL;
			}
			/* painted stays: surface pixels remain valid until overwritten */
			st->trimmed++;
			n++;
		}
	return (int)n;
}

void rdp_progressive_free(struct rdp_prog_surface* ps, const struct rdp_mem* mem)
{
	size_t i, n;
	if (!ps->tiles)
		return;
	n = (size_t)ps->gw * ps->gh;
	for (i = 0; i < n; i++)
	{
		struct rdp_prog_tile* t = ps->tiles[i];
		if (!t)
			continue;
		rdp_mem_free(mem, t->coef);
		if (t->sign)
			rdp_mem_free(mem, t->sign);
		rdp_mem_free(mem, t);
	}
	rdp_mem_free(mem, ps->tiles);
	ps->tiles = NULL;
	ps->gw = ps->gh = 0;
}
