/* MD4, written from RFC 1320 (section 3). MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gn_internal.h"
#include <string.h>

#define F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define G(x, y, z) (((x) & (y)) | ((x) & (z)) | ((y) & (z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define ROL(x, s) (((x) << (s)) | ((x) >> (32 - (s))))

static void md4_block(uint32_t h[4], const uint8_t *p)
{
	uint32_t x[16], a = h[0], b = h[1], c = h[2], d = h[3];
	int i;
	for (i = 0; i < 16; i++)
		x[i] = gn_get32(p + 4 * i);
	/* round 1: [abcd k s] a = (a + F(b,c,d) + X[k]) <<< s */
#define R1(a, b, c, d, k, s) a = ROL(a + F(b, c, d) + x[k], s)
	for (i = 0; i < 16; i += 4) {
		R1(a, b, c, d, i, 3); R1(d, a, b, c, i + 1, 7);
		R1(c, d, a, b, i + 2, 11); R1(b, c, d, a, i + 3, 19);
	}
	/* round 2: a = (a + G(b,c,d) + X[k] + 5A827999) <<< s */
#define R2(a, b, c, d, k, s) a = ROL(a + G(b, c, d) + x[k] + 0x5a827999u, s)
	for (i = 0; i < 4; i++) {
		R2(a, b, c, d, i, 3); R2(d, a, b, c, i + 4, 5);
		R2(c, d, a, b, i + 8, 9); R2(b, c, d, a, i + 12, 13);
	}
	/* round 3: a = (a + H(b,c,d) + X[k] + 6ED9EBA1) <<< s */
#define R3(a, b, c, d, k, s) a = ROL(a + H(b, c, d) + x[k] + 0x6ed9eba1u, s)
	{
		static const uint8_t o[4] = { 0, 2, 1, 3 };
		for (i = 0; i < 4; i++) {
			R3(a, b, c, d, o[i], 3); R3(d, a, b, c, o[i] + 8, 9);
			R3(c, d, a, b, o[i] + 4, 11); R3(b, c, d, a, o[i] + 12, 15);
		}
	}
	h[0] += a; h[1] += b; h[2] += c; h[3] += d;
}

void gn_md4_init(struct gn_md4 *c)
{
	c->h[0] = 0x67452301u; c->h[1] = 0xefcdab89u;
	c->h[2] = 0x98badcfeu; c->h[3] = 0x10325476u;
	c->len = 0;
}

void gn_md4_update(struct gn_md4 *c, const void *data, size_t len)
{
	const uint8_t *p = data;
	size_t used = (size_t)(c->len & 63);
	c->len += len;
	while (len) {
		size_t n = 64 - used;
		if (n > len) n = len;
		memcpy(c->buf + used, p, n);
		used += n; p += n; len -= n;
		if (used == 64) { md4_block(c->h, c->buf); used = 0; }
	}
}

void gn_md4_final(struct gn_md4 *c, uint8_t out[16])
{
	uint8_t pad[72];
	uint64_t bits = c->len * 8;
	size_t used = (size_t)(c->len & 63), n = (used < 56 ? 56 : 120) - used;
	int i;
	memset(pad, 0, sizeof pad);
	pad[0] = 0x80;
	for (i = 0; i < 8; i++)
		pad[n + i] = (uint8_t)(bits >> (8 * i));
	gn_md4_update(c, pad, n + 8);
	for (i = 0; i < 4; i++)
		gn_put32(out + 4 * i, c->h[i]);
	gn_wipe(c, sizeof *c);
}

void gn_md4(const void *data, size_t len, uint8_t out[16])
{
	struct gn_md4 c;
	gn_md4_init(&c);
	gn_md4_update(&c, data, len);
	gn_md4_final(&c, out);
}
