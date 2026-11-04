/* gfopenntlm: the DER subset that [MS-CSSP] needs, written from
 * ITU-T X.690 (8.1 identifier and length octets, definite form only).
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gn_internal.h"

size_t gn_der_hdr_size(size_t len)
{
	if (len < 0x80) return 2;
	if (len < 0x100) return 3;
	if (len < 0x10000) return 4;
	return 5;
}

size_t gn_der_hdr(uint8_t *p, uint8_t tag, size_t len)
{
	size_t n = gn_der_hdr_size(len);
	if (p) {
		p[0] = tag;
		if (n == 2) p[1] = (uint8_t)len;
		else {
			size_t i, k = n - 2;
			p[1] = (uint8_t)(0x80 | k);
			for (i = 0; i < k; i++)
				p[2 + i] = (uint8_t)(len >> (8 * (k - 1 - i)));
		}
	}
	return n;
}

int gn_der_peek(const struct gn_der *d)
{
	return d->len ? d->p[0] : -1;
}

int gn_der_get(struct gn_der *d, uint8_t tag, struct gn_der *v)
{
	size_t len, hl = 2;
	if (d->len < 2 || d->p[0] != tag) return -1;
	len = d->p[1];
	if (len & 0x80) {
		size_t k = len & 0x7f, i;
		if (k == 0 || k > 3 || d->len < 2 + k) return -1;  /* no indefinite form in DER */
		len = 0;
		for (i = 0; i < k; i++) len = (len << 8) | d->p[2 + i];
		hl += k;
	}
	if (len > d->len - hl) return -1;
	v->p = d->p + hl;
	v->len = len;
	d->p += hl + len;
	d->len -= hl + len;
	return 0;
}

long gn_der_message_length(const uint8_t *p, size_t have)
{
	size_t k, i, len;
	if (have < 2) return 0;
	if (!(p[1] & 0x80)) return 2 + p[1];
	k = p[1] & 0x7f;
	if (k == 0 || k > 3) return -1;
	if (have < 2 + k) return 0;
	for (len = 0, i = 0; i < k; i++) len = (len << 8) | p[2 + i];
	return (long)(2 + k + len);
}
