/* RC4, written from the public description of the cipher (the key
 * scheduling and output loops, as in the RFC 6229 test vector document).
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gn_internal.h"

void gn_rc4_init(struct gn_rc4 *r, const void *key, size_t klen)
{
	const uint8_t *k = key;
	unsigned i, j = 0;
	for (i = 0; i < 256; i++)
		r->s[i] = (uint8_t)i;
	for (i = 0; i < 256; i++) {
		uint8_t t = r->s[i];
		j = (j + t + k[i % klen]) & 255;
		r->s[i] = r->s[j];
		r->s[j] = t;
	}
	r->i = r->j = 0;
}

void gn_rc4_crypt(struct gn_rc4 *r, const void *in, void *out, size_t len)
{
	const uint8_t *p = in;
	uint8_t *q = out, i = r->i, j = r->j;
	while (len--) {
		uint8_t t;
		i = (uint8_t)(i + 1);
		t = r->s[i];
		j = (uint8_t)(j + t);
		r->s[i] = r->s[j];
		r->s[j] = t;
		*q++ = *p++ ^ r->s[(uint8_t)(r->s[i] + t)];
	}
	r->i = i; r->j = j;
}
