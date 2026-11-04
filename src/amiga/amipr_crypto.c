/*
 * amipr: hashes, AES, RSA and random bytes.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 *
 * SHA-256/384/512, AES and the RSA public operation come from
 * gfopencrypto.library (which runs them as x86/ARM64 code on AmiCradle).
 * Its hash calls are one-shot or whole-block; the streaming interface
 * here keeps the partial block and does the padding (FIPS 180-4, 5.1).
 * SHA-1 is not in gfopencrypto.library and is implemented here, from
 * FIPS 180-4 section 6.1. Random bytes come from gfopentls.library's
 * OT_Random().
 */
#include "amipr_internal.h"

#include <string.h>
#include <libraries/gfopencrypto.h>
#include <libraries/gfopentls.h>
#include <proto/gfopencrypto.h>
#include <proto/gfopentls.h>

struct Library* OpenCryptoBase;
struct Library* OpenTLSBase;

static struct Library* open_once(struct Library** base, const char* name, ULONG version)
{
	if (*base)
		return *base;
	{
		struct Library* b = OpenLibrary((CONST_STRPTR)name, version);
		Forbid();
		if (!*base)
		{
			*base = b;
			b = NULL;
		}
		Permit();
		if (b)
			CloseLibrary(b); /* somebody else was quicker */
	}
	return *base;
}

static struct Library* crypto_lib(void)
{
	return open_once(&OpenCryptoBase, OPENCRYPTOLIB_NAME, OPENCRYPTOLIB_VERSION);
}

struct Library* amipr_opentls_base(void)
{
	return open_once(&OpenTLSBase, OPENTLSLIB_NAME, OPENTLSLIB_VERSION);
}

int amipr_random(void* buf, size_t len)
{
	if (!buf)
		return AMIPR_EINVAL;
	if (!amipr_opentls_base())
		return AMIPR_ENOLIB;
	while (len)
	{
		LONG n = len > 0x10000 ? 0x10000 : (LONG)len;
		if (OT_Random(buf, n) != OTERR_OK)
			return AMIPR_ENOLIB;
		buf = (uint8_t*)buf + n;
		len -= (size_t)n;
	}
	return AMIPR_OK;
}

/* ---- SHA-1 (FIPS 180-4, 6.1) ---- */

static uint32_t rol32(uint32_t x, int n)
{
	return (x << n) | (x >> (32 - n));
}

static uint32_t be32(const uint8_t* p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void put_be32(uint8_t* p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static void sha1_block(uint8_t* state, const uint8_t* m)
{
	uint32_t w[80];
	uint32_t a = be32(state), b = be32(state + 4), c = be32(state + 8), d = be32(state + 12),
	         e = be32(state + 16);
	int t;

	for (t = 0; t < 16; t++)
		w[t] = be32(m + 4 * t);
	for (t = 16; t < 80; t++)
		w[t] = rol32(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
	for (t = 0; t < 80; t++)
	{
		uint32_t f, k, tmp;
		if (t < 20)
		{
			f = (b & c) | (~b & d);
			k = 0x5a827999UL;
		}
		else if (t < 40)
		{
			f = b ^ c ^ d;
			k = 0x6ed9eba1UL;
		}
		else if (t < 60)
		{
			f = (b & c) | (b & d) | (c & d);
			k = 0x8f1bbcdcUL;
		}
		else
		{
			f = b ^ c ^ d;
			k = 0xca62c1d6UL;
		}
		tmp = rol32(a, 5) + f + e + k + w[t];
		e = d;
		d = c;
		c = rol32(b, 30);
		b = a;
		a = tmp;
	}
	put_be32(state, be32(state) + a);
	put_be32(state + 4, be32(state + 4) + b);
	put_be32(state + 8, be32(state + 8) + c);
	put_be32(state + 12, be32(state + 12) + d);
	put_be32(state + 16, be32(state + 16) + e);
}

/* ---- streaming over whole-block functions ---- */

size_t amipr_hash_size(int alg)
{
	switch (alg)
	{
		case AMIPR_SHA1: return 20;
		case AMIPR_SHA256: return 32;
		case AMIPR_SHA384: return 48;
		case AMIPR_SHA512: return 64;
		default: return 0;
	}
}

size_t amipr_hash_block_size(int alg)
{
	return (alg == AMIPR_SHA384 || alg == AMIPR_SHA512) ? 128 : (alg ? 64 : 0);
}

static int run_blocks(struct amipr_hash* h, const void* data, size_t nblocks)
{
	struct OCBlocksRequest req;
	struct Library* OpenCryptoBase;

	if (h->alg == AMIPR_SHA1)
	{
		const uint8_t* p = data;
		while (nblocks--)
		{
			sha1_block(h->state, p);
			p += 64;
		}
		return AMIPR_OK;
	}
	OpenCryptoBase = crypto_lib();
	if (!OpenCryptoBase)
		return AMIPR_ENOLIB;
	req.state = h->state;
	req.data = data;
	req.blocks = (ULONG)nblocks;
	if (h->alg == AMIPR_SHA256)
		return OC_SHA256Blocks(&req) == 0 ? AMIPR_OK : AMIPR_EINVAL;
	return OC_SHA512Blocks(&req) == 0 ? AMIPR_OK : AMIPR_EINVAL;
}

/* initial hash values, FIPS 180-4 section 5.3 */
static const uint32_t iv_sha1[5] = { 0x67452301UL, 0xefcdab89UL, 0x98badcfeUL, 0x10325476UL,
	                                 0xc3d2e1f0UL };
static const uint32_t iv_sha256[8] = { 0x6a09e667UL, 0xbb67ae85UL, 0x3c6ef372UL, 0xa54ff53aUL,
	                                   0x510e527fUL, 0x9b05688cUL, 0x1f83d9abUL, 0x5be0cd19UL };
static const uint32_t iv_sha384[16] = { 0xcbbb9d5dUL, 0xc1059ed8UL, 0x629a292aUL, 0x367cd507UL,
	                                    0x9159015aUL, 0x3070dd17UL, 0x152fecd8UL, 0xf70e5939UL,
	                                    0x67332667UL, 0xffc00b31UL, 0x8eb44a87UL, 0x68581511UL,
	                                    0xdb0c2e0dUL, 0x64f98fa7UL, 0x47b5481dUL, 0xbefa4fa4UL };
static const uint32_t iv_sha512[16] = { 0x6a09e667UL, 0xf3bcc908UL, 0xbb67ae85UL, 0x84caa73bUL,
	                                    0x3c6ef372UL, 0xfe94f82bUL, 0xa54ff53aUL, 0x5f1d36f1UL,
	                                    0x510e527fUL, 0xade682d1UL, 0x9b05688cUL, 0x2b3e6c1fUL,
	                                    0x1f83d9abUL, 0xfb41bd6bUL, 0x5be0cd19UL, 0x137e2179UL };

int amipr_hash_init(struct amipr_hash* h, int alg)
{
	const uint32_t* iv;
	size_t words, i;

	if (!h)
		return AMIPR_EINVAL;
	switch (alg)
	{
		case AMIPR_SHA1: iv = iv_sha1; words = 5; break;
		case AMIPR_SHA256: iv = iv_sha256; words = 8; break;
		case AMIPR_SHA384: iv = iv_sha384; words = 16; break;
		case AMIPR_SHA512: iv = iv_sha512; words = 16; break;
		default: return AMIPR_EINVAL;
	}
	if (alg != AMIPR_SHA1 && !crypto_lib())
		return AMIPR_ENOLIB;
	memset(h, 0, sizeof(*h));
	h->alg = alg;
	for (i = 0; i < words; i++)
		put_be32(h->state + 4 * i, iv[i]);
	return AMIPR_OK;
}

int amipr_hash_update(struct amipr_hash* h, const void* data, size_t len)
{
	const uint8_t* p = data;
	size_t bs;
	int rc;

	if (!h || (!data && len))
		return AMIPR_EINVAL;
	bs = amipr_hash_block_size(h->alg);
	h->total += len;
	if (h->used)
	{
		size_t take = bs - h->used;
		if (take > len)
			take = len;
		memcpy(h->block + h->used, p, take);
		h->used += take;
		p += take;
		len -= take;
		if (h->used < bs)
			return AMIPR_OK;
		if ((rc = run_blocks(h, h->block, 1)) != AMIPR_OK)
			return rc;
		h->used = 0;
	}
	if (len >= bs)
	{
		size_t n = len / bs;
		if ((rc = run_blocks(h, p, n)) != AMIPR_OK)
			return rc;
		p += n * bs;
		len -= n * bs;
	}
	memcpy(h->block, p, len);
	h->used = len;
	return AMIPR_OK;
}

int amipr_hash_final(struct amipr_hash* h, uint8_t* out, size_t out_size)
{
	size_t bs, lenbytes, dlen;
	uint64_t bits;
	int rc, i;

	if (!h || !out)
		return AMIPR_EINVAL;
	bs = amipr_hash_block_size(h->alg);
	dlen = amipr_hash_size(h->alg);
	if (out_size < dlen)
		return AMIPR_EINVAL;
	lenbytes = bs == 128 ? 16 : 8;
	bits = h->total * 8;
	h->block[h->used++] = 0x80;
	if (h->used > bs - lenbytes)
	{
		memset(h->block + h->used, 0, bs - h->used);
		if ((rc = run_blocks(h, h->block, 1)) != AMIPR_OK)
			return rc;
		h->used = 0;
	}
	memset(h->block + h->used, 0, bs - h->used);
	for (i = 0; i < 8; i++)
		h->block[bs - 1 - i] = (uint8_t)(bits >> (8 * i));
	if ((rc = run_blocks(h, h->block, 1)) != AMIPR_OK)
		return rc;
	memcpy(out, h->state, dlen);
	memset(h, 0, sizeof(*h));
	return (int)dlen;
}

int amipr_aes(uint8_t* buf, size_t len, const uint8_t* key, size_t keylen, const uint8_t iv[16],
              int mode)
{
	struct Library* OpenCryptoBase = crypto_lib();
	struct OCAESRequest req;
	if (!OpenCryptoBase)
		return AMIPR_ENOLIB;
	req.buf = buf;
	req.length = (ULONG)len;
	req.key = key;
	req.key_length = (ULONG)keylen;
	req.iv = iv;
	req.mode = (ULONG)mode;
	return OC_AES(&req) == 0 ? AMIPR_OK : AMIPR_EINVAL;
}

int amipr_rsa_public(uint8_t* out, const uint8_t* in, const uint8_t* n, size_t n_len,
                     const uint8_t* e, size_t e_len)
{
	struct Library* OpenCryptoBase = crypto_lib();
	struct OCRSARequest req;
	if (!OpenCryptoBase)
		return AMIPR_ENOLIB;
	req.out = out;
	req.sig = in;
	req.n = n;
	req.n_length = (ULONG)n_len;
	req.e = e;
	req.e_length = (ULONG)e_len;
	return OC_RSAPublic(&req) == 0 ? AMIPR_OK : AMIPR_EINVAL;
}
