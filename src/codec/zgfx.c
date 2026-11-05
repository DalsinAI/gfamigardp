/*
 * GFAmigaRDP: RDP 8.0 Bulk decompression ([MS-RDPEGFX] 2.2.5, 3.1.9.1).
 * The token table is the one in 3.1.9.1.2.4 (literal, match distance and
 * the unencoded-run escape), the length codes are the match length
 * prefixes that follow it. Bits are read most significant first; the last
 * byte of a compressed segment says how many bits of the one before it
 * are padding. Plain C99.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include <string.h>
#include "zgfx.h"

#define SEG_SINGLE 0xE0
#define SEG_MULTIPART 0xE1
#define PACKET_COMPRESSED 0x20
#define COMPR_TYPE_MASK 0x0F
#define COMPR_TYPE_RDP8 0x04

/* prefix length, prefix code, value bits, 0 literal / 1 match, value base
 * ([MS-RDPEGFX] 3.1.9.1.2.4), in order of prefix length */
static const struct
{
	uint8_t plen;
	uint16_t code;
	uint8_t vbits;
	uint8_t match;
	uint32_t base;
} tokens[] = {
	{ 1, 0, 8, 0, 0 },
	{ 5, 17, 5, 1, 0 },
	{ 5, 18, 7, 1, 32 },
	{ 5, 19, 9, 1, 160 },
	{ 5, 20, 10, 1, 672 },
	{ 5, 21, 12, 1, 1696 },
	{ 5, 24, 0, 0, 0x00 },
	{ 5, 25, 0, 0, 0x01 },
	{ 6, 44, 14, 1, 5792 },
	{ 6, 45, 15, 1, 22176 },
	{ 6, 52, 0, 0, 0x02 },
	{ 6, 53, 0, 0, 0x03 },
	{ 6, 54, 0, 0, 0xFF },
	{ 7, 92, 18, 1, 54944 },
	{ 7, 93, 20, 1, 317088 },
	{ 7, 110, 0, 0, 0x04 },
	{ 7, 111, 0, 0, 0x05 },
	{ 7, 112, 0, 0, 0x06 },
	{ 7, 113, 0, 0, 0x07 },
	{ 7, 114, 0, 0, 0x08 },
	{ 7, 115, 0, 0, 0x09 },
	{ 7, 116, 0, 0, 0x0A },
	{ 7, 117, 0, 0, 0x0B },
	{ 7, 118, 0, 0, 0x3A },
	{ 7, 119, 0, 0, 0x3B },
	{ 7, 120, 0, 0, 0x3C },
	{ 7, 121, 0, 0, 0x3D },
	{ 7, 122, 0, 0, 0x3E },
	{ 7, 123, 0, 0, 0x3F },
	{ 7, 124, 0, 0, 0x40 },
	{ 7, 125, 0, 0, 0x80 },
	{ 8, 188, 20, 1, 1365664 },
	{ 8, 189, 21, 1, 2414240 },
	{ 8, 252, 0, 0, 0x0C },
	{ 8, 253, 0, 0, 0x38 },
	{ 8, 254, 0, 0, 0x39 },
	{ 8, 255, 0, 0, 0x66 },
	{ 9, 380, 22, 1, 4511392 },
	{ 9, 381, 23, 1, 8705696 },
	{ 9, 382, 24, 1, 17094304 },
};
#define NTOKENS (sizeof(tokens) / sizeof(tokens[0]))

struct bits
{
	const uint8_t* p;
	size_t pos, end;   /* in bits */
	int err;
};

static uint32_t get(struct bits* b, unsigned n)
{
	uint32_t v = 0;
	if (b->err || b->end - b->pos < n)
	{
		b->err = 1;
		return 0;
	}
	while (n--)
	{
		v = (v << 1) | ((b->p[b->pos >> 3] >> (7 - (b->pos & 7))) & 1);
		b->pos++;
	}
	return v;
}

int rdp_zgfx_init(struct rdp_zgfx* z, const struct rdp_mem* mem)
{
	memset(z, 0, sizeof(*z));
	if (mem)
		z->mem = *mem;
	z->hist = (uint8_t*)rdp_mem_alloc(&z->mem, RDP_ZGFX_HISTORY);
	if (!z->hist)
		return RDP_ZGFX_E_NOMEM;
	memset(z->hist, 0, RDP_ZGFX_HISTORY);
	return 0;
}

void rdp_zgfx_free(struct rdp_zgfx* z)
{
	rdp_mem_free(&z->mem, z->hist);
	rdp_mem_free(&z->mem, z->out);
	z->hist = NULL;
	z->out = NULL;
	z->out_len = z->out_cap = 0;
}

static int reserve(struct rdp_zgfx* z, size_t need)
{
	uint8_t* n;
	size_t cap;
	if (need <= z->out_cap)
		return 0;
	if (need > RDP_ZGFX_OUT_MAX)
		return RDP_ZGFX_E_PROTO;
	cap = z->out_cap ? z->out_cap : 65536;
	while (cap < need)
		cap *= 2;
	if (cap > RDP_ZGFX_OUT_MAX)
		cap = RDP_ZGFX_OUT_MAX;
	n = (uint8_t*)rdp_mem_alloc(&z->mem, cap);
	if (!n)
		return RDP_ZGFX_E_NOMEM;
	if (z->out_len)
		memcpy(n, z->out, z->out_len);
	rdp_mem_free(&z->mem, z->out);
	z->out = n;
	z->out_cap = cap;
	return 0;
}

static void put(struct rdp_zgfx* z, uint8_t c)
{
	z->hist[z->hpos] = c;
	if (++z->hpos == RDP_ZGFX_HISTORY)
		z->hpos = 0;
	z->out[z->out_len++] = c;
}

/* one RDP8_BULK_ENCODED_DATA (header byte + data), appended to z->out */
static int segment(struct rdp_zgfx* z, const uint8_t* p, size_t len)
{
	size_t start = z->out_len, i;
	struct bits b;
	int rc;

	if (len < 1 || (p[0] & COMPR_TYPE_MASK) != COMPR_TYPE_RDP8)
		return RDP_ZGFX_E_PROTO;
	p++;
	len--;
	if ((rc = reserve(z, start + RDP_ZGFX_SEGMENT_MAX)) < 0)
		return rc;
	if (!(p[-1] & PACKET_COMPRESSED))
	{
		if (len > RDP_ZGFX_SEGMENT_MAX)
			return RDP_ZGFX_E_PROTO;
		for (i = 0; i < len; i++)
			put(z, p[i]);
		return 0;
	}
	/* the last byte: how many bits of the one before it are unused */
	if (len < 1 || p[len - 1] > 7)
		return RDP_ZGFX_E_PROTO;
	b.p = p;
	b.pos = 0;
	b.end = 8 * (len - 1);
	if (b.end < p[len - 1])
		return RDP_ZGFX_E_PROTO;
	b.end -= p[len - 1];
	b.err = 0;

	while (b.pos < b.end)
	{
		uint32_t prefix = 0, v, dist, count;
		unsigned have = 0, t;
		for (t = 0; t < NTOKENS; t++)
		{
			while (have < tokens[t].plen)
			{
				prefix = (prefix << 1) | get(&b, 1);
				have++;
			}
			if (b.err)
				return RDP_ZGFX_E_PROTO;
			if (prefix == tokens[t].code)
				break;
		}
		if (t == NTOKENS)
			return RDP_ZGFX_E_PROTO;   /* a reserved code */
		v = get(&b, tokens[t].vbits);
		if (!tokens[t].match)
		{
			if (b.err || z->out_len - start >= RDP_ZGFX_SEGMENT_MAX)
				return RDP_ZGFX_E_PROTO;
			put(z, (uint8_t)(tokens[t].base + v));
			continue;
		}
		dist = tokens[t].base + v;
		if (dist == 0)
		{
			/* unencoded run: 15-bit count, then whole bytes */
			count = get(&b, 15);
			b.pos = (b.pos + 7) & ~(size_t)7;
			if (b.err || b.pos > b.end || (b.end - b.pos) / 8 < count ||
			    z->out_len - start + count > RDP_ZGFX_SEGMENT_MAX)
				return RDP_ZGFX_E_PROTO;
			for (i = 0; i < count; i++)
				put(z, p[b.pos / 8 + i]);
			b.pos += (size_t)8 * count;
			continue;
		}
		if (!get(&b, 1))
			count = 3;
		else
		{
			unsigned extra = 2;
			count = 4;
			while (get(&b, 1) == 1)
			{
				if (b.err || extra >= 15)
					return RDP_ZGFX_E_PROTO;
				count *= 2;
				extra++;
			}
			count += get(&b, extra);
		}
		if (b.err || dist > RDP_ZGFX_HISTORY || z->out_len - start + count > RDP_ZGFX_SEGMENT_MAX)
			return RDP_ZGFX_E_PROTO;
		{
			uint32_t from = (z->hpos + RDP_ZGFX_HISTORY - dist) % RDP_ZGFX_HISTORY;
			/* byte by byte: an overlapping match replicates */
			while (count--)
			{
				uint8_t c = z->hist[from];
				if (++from == RDP_ZGFX_HISTORY)
					from = 0;
				put(z, c);
			}
		}
	}
	return 0;
}

static uint32_t le32(const uint8_t* p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int rdp_zgfx_decompress(struct rdp_zgfx* z, const uint8_t* in, size_t len,
                        const uint8_t** out, size_t* out_len)
{
	int rc;
	z->out_len = 0;
	if (!z->hist || len < 1)
		return RDP_ZGFX_E_PROTO;
	if (in[0] == SEG_SINGLE)
	{
		rc = segment(z, in + 1, len - 1);
	}
	else if (in[0] == SEG_MULTIPART)
	{
		uint16_t count, i;
		uint32_t total;
		size_t pos = 7;
		if (len < 7)
			return RDP_ZGFX_E_PROTO;
		count = (uint16_t)(in[1] | (in[2] << 8));
		total = le32(in + 3);
		if (total > RDP_ZGFX_OUT_MAX)
			return RDP_ZGFX_E_PROTO;
		if ((rc = reserve(z, total)) < 0)
			return rc;
		for (i = 0, rc = 0; i < count && rc == 0; i++)
		{
			uint32_t sz;
			if (len - pos < 4)
				return RDP_ZGFX_E_PROTO;
			sz = le32(in + pos);
			pos += 4;
			if (len - pos < sz)
				return RDP_ZGFX_E_PROTO;
			rc = segment(z, in + pos, sz);
			pos += sz;
		}
		if (rc == 0 && z->out_len != total)
			rc = RDP_ZGFX_E_PROTO;
	}
	else
		return RDP_ZGFX_E_PROTO;
	if (rc < 0)
		return rc;
	*out = z->out;
	*out_len = z->out_len;
	return 0;
}
