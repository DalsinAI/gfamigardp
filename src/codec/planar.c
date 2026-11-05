/*
 * GFAmigaRDP: Planar (RDP 6.0 Bitmap Compression) and Uncompressed decode.
 * [MS-RDPEGDI] 2.2.2.5.1 (format header and planes), 2.2.2.5.1.2 (RLE
 * segment control byte), 3.1.9.1.2 (AYCoCg inverse transform, clamping),
 * 3.1.9.1.3 (chroma super-sampling), 3.1.9.1.4 (colour loss), 3.1.9.2
 * (scan-line deltas and their sign coding). [MS-RDPEGFX] 2.2.1.3 and
 * 2.2.2.1 for the uncompressed pixel order. Plain C99.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include <string.h>
#include "planar.h"

long rdp_planar_rle_plane(const uint8_t* src, size_t len, uint32_t w, uint32_t h, uint8_t* out)
{
	size_t pos = 0;
	uint32_t y;
	for (y = 0; y < h; y++)
	{
		uint8_t* row = out + (size_t)y * w;
		const uint8_t* above = y ? row - w : NULL;
		uint32_t x = 0;
		uint8_t last = 0;   /* a RUN with no RAW before it in the scan line repeats 0 */
		while (x < w)
		{
			uint8_t cb;
			uint32_t run, raw, i;
			if (pos >= len)
				return RDP_PLANAR_E_PROTO;
			cb = src[pos++];
			run = cb & 0x0F;
			raw = cb >> 4;
			if (run == 1)
			{
				run = 16 + raw;
				raw = 0;
			}
			else if (run == 2)
			{
				run = 32 + raw;
				raw = 0;
			}
			if (len - pos < raw || w - x < raw + run)
				return RDP_PLANAR_E_PROTO;
			for (i = 0; i < raw; i++)
			{
				last = src[pos++];
				row[x++] = last;
			}
			for (i = 0; i < run; i++)
				row[x++] = last;
		}
		if (above)
		{
			/* deltas: odd v is 255 - (v - 1) / 2, even v is v / 2, added to the line above */
			for (x = 0; x < w; x++)
			{
				uint8_t v = row[x];
				uint8_t d = (v & 1) ? (uint8_t)(255 - ((v - 1) >> 1)) : (uint8_t)(v >> 1);
				row[x] = (uint8_t)(above[x] + d);
			}
		}
	}
	return (long)pos;
}

size_t rdp_planar_scratch(uint32_t w, uint32_t h)
{
	return (size_t)w * h * 4;
}

static uint8_t clamp(int v)
{
	return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

int rdp_planar_decode(const uint8_t* src, size_t len, uint32_t w, uint32_t h,
                      uint32_t* dst, uint32_t dst_stride, uint8_t* scratch, unsigned opts)
{
	uint8_t hdr, cll;
	int cs, rle, alpha;
	uint32_t cw, ch, x, y, p;
	size_t pos = 1, plane_sz[4];
	uint8_t* planes[4];

	if (!w || !h || len < 1)
		return RDP_PLANAR_E_PROTO;
	hdr = src[0];
	cll = hdr & RDP_PLANAR_CLL_MASK;
	cs = (hdr & RDP_PLANAR_CS) != 0;
	rle = (hdr & RDP_PLANAR_RLE) != 0;
	alpha = !(hdr & RDP_PLANAR_NA);
	if (cs && !cll)
		return RDP_PLANAR_E_PROTO;   /* 2.2.2.5.1: CS needs AYCoCg */
	cw = cs ? (w + 1) / 2 : w;
	ch = cs ? (h + 1) / 2 : h;
	plane_sz[0] = (size_t)w * h;
	plane_sz[1] = (size_t)w * h;
	plane_sz[2] = plane_sz[3] = (size_t)cw * ch;
	planes[0] = scratch;
	planes[1] = planes[0] + plane_sz[0];
	planes[2] = planes[1] + plane_sz[1];
	planes[3] = planes[2] + plane_sz[2];

	for (p = alpha ? 0 : 1; p < 4; p++)
	{
		uint32_t pw = p >= 2 ? cw : w, ph = p >= 2 ? ch : h;
		if (rle)
		{
			long n = rdp_planar_rle_plane(src + pos, len - pos, pw, ph, planes[p]);
			if (n < 0)
				return (int)n;
			pos += (size_t)n;
		}
		else
		{
			if (len - pos < plane_sz[p])
				return RDP_PLANAR_E_PROTO;
			memcpy(planes[p], src + pos, plane_sz[p]);
			pos += plane_sz[p];
		}
	}
	/* a raw stream may end in one pad byte (2.2.2.5.1); anything else is left over */
	if (!alpha)
		memset(planes[0], 0xFF, plane_sz[0]);

	for (y = 0; y < h; y++)
	{
		uint32_t* out = dst + (size_t)y * dst_stride;
		const uint8_t* A = planes[0] + (size_t)y * w;
		const uint8_t* P1 = planes[1] + (size_t)y * w;
		if (!cll)
		{
			const uint8_t* G = planes[2] + (size_t)y * w;
			const uint8_t* B = planes[3] + (size_t)y * w;
			for (x = 0; x < w; x++)
				out[x] = ((uint32_t)A[x] << 24) | ((uint32_t)P1[x] << 16) | ((uint32_t)G[x] << 8) | B[x];
			continue;
		}
		{
			/* AYCoCg: Co, Cg come shifted right by CLL (3.1.9.1.4); shift them
			 * back inserting zeros, read as signed 8 bits, then the inverse
			 * matrix of 3.1.9.1.2 (the 1/2 terms) */
			const uint8_t* Co = planes[2] + (size_t)(cs ? y / 2 : y) * cw;
			const uint8_t* Cg = planes[3] + (size_t)(cs ? y / 2 : y) * cw;
			for (x = 0; x < w; x++)
			{
				uint32_t cx = cs ? x / 2 : x;
				int Y = P1[x];
				int co = (int)(int8_t)(uint8_t)(Co[cx] << cll) / 2;
				int cg = (int)(int8_t)(uint8_t)(Cg[cx] << cll) / 2;
				int r = Y + co - cg, g = Y + cg, b = Y - co - cg;
				if (opts & RDP_PLANAR_SWAP_RB)
				{
					int t = r;
					r = b;
					b = t;
				}
				out[x] = ((uint32_t)A[x] << 24) | ((uint32_t)clamp(r) << 16) | ((uint32_t)clamp(g) << 8) | clamp(b);
			}
		}
	}
	return 0;
}

int rdp_uncompressed_decode(const uint8_t* src, size_t len, uint32_t w, uint32_t h,
                            uint32_t* dst, uint32_t dst_stride, int has_alpha)
{
	uint32_t x, y;
	if ((size_t)w * h * 4 != len)
		return RDP_PLANAR_E_PROTO;
	for (y = 0; y < h; y++)
	{
		uint32_t* out = dst + (size_t)y * dst_stride;
		for (x = 0; x < w; x++, src += 4)
			out[x] = ((uint32_t)(has_alpha ? src[3] : 0xFF) << 24) | ((uint32_t)src[2] << 16) |
			         ((uint32_t)src[1] << 8) | src[0];
	}
	return 0;
}
