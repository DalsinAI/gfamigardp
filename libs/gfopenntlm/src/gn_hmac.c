/* HMAC, written from RFC 2104 (section 2): H(K ^ opad, H(K ^ ipad, text)).
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gn_internal.h"
#include <string.h>

void gn_hmac_md5_init(struct gn_hmac_md5 *h, const void *key, size_t klen)
{
	uint8_t k[64], kh[16];
	int i;
	memset(k, 0, sizeof k);
	if (klen > 64) { gn_md5(key, klen, kh); memcpy(k, kh, 16); }
	else if (klen) memcpy(k, key, klen);
	for (i = 0; i < 64; i++) k[i] ^= 0x36;
	gn_md5_init(&h->inner);
	gn_md5_update(&h->inner, k, 64);
	for (i = 0; i < 64; i++) k[i] ^= 0x36 ^ 0x5c;
	gn_md5_init(&h->outer);
	gn_md5_update(&h->outer, k, 64);
	gn_wipe(k, sizeof k);
	gn_wipe(kh, sizeof kh);
}

void gn_hmac_md5_update(struct gn_hmac_md5 *h, const void *data, size_t len)
{
	if (len) gn_md5_update(&h->inner, data, len);
}

void gn_hmac_md5_final(struct gn_hmac_md5 *h, uint8_t out[16])
{
	uint8_t ih[16];
	gn_md5_final(&h->inner, ih);
	gn_md5_update(&h->outer, ih, 16);
	gn_md5_final(&h->outer, out);
	gn_wipe(ih, sizeof ih);
}

void gn_hmac_md5(const void *key, size_t klen, const void *data, size_t len, uint8_t out[16])
{
	struct gn_hmac_md5 h;
	gn_hmac_md5_init(&h, key, klen);
	gn_hmac_md5_update(&h, data, len);
	gn_hmac_md5_final(&h, out);
}

void gn_hmac_sha256(const void *key, size_t klen, const void *data, size_t len, uint8_t out[32])
{
	struct gn_sha256 in, ou;
	uint8_t k[64], ih[32];
	int i;
	memset(k, 0, sizeof k);
	if (klen > 64) gn_sha256(key, klen, k);
	else if (klen) memcpy(k, key, klen);
	for (i = 0; i < 64; i++) k[i] ^= 0x36;
	gn_sha256_init(&in);
	gn_sha256_update(&in, k, 64);
	if (len) gn_sha256_update(&in, data, len);
	gn_sha256_final(&in, ih);
	for (i = 0; i < 64; i++) k[i] ^= 0x36 ^ 0x5c;
	gn_sha256_init(&ou);
	gn_sha256_update(&ou, k, 64);
	gn_sha256_update(&ou, ih, 32);
	gn_sha256_final(&ou, out);
	gn_wipe(k, sizeof k);
	gn_wipe(ih, sizeof ih);
}
