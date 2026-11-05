/*
 * GFAmigaRDP: ClearCodec decoder ([MS-RDPEGFX] 2.2.4.1, examples 4.1.1.1-4).
 * Plain C99. MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include <string.h>
#include "clearcodec.h"

static uint32_t px_bgr(uint8_t b, uint8_t g, uint8_t r)
{
	return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint16_t rd_u16(const uint8_t** p, const uint8_t* e, int* err)
{
	uint16_t v;
	if (*err || *p + 2 > e) { *err = 1; return 0; }
	v = (uint16_t)(*p)[0] | ((uint16_t)(*p)[1] << 8);
	*p += 2;
	return v;
}

static uint32_t rd_u32(const uint8_t** p, const uint8_t* e, int* err)
{
	uint32_t v;
	if (*err || *p + 4 > e) { *err = 1; return 0; }
	v = (uint32_t)(*p)[0] | ((uint32_t)(*p)[1] << 8) |
	    ((uint32_t)(*p)[2] << 16) | ((uint32_t)(*p)[3] << 24);
	*p += 4;
	return v;
}

static uint8_t rd_u8(const uint8_t** p, const uint8_t* e, int* err)
{
	if (*err || *p >= e) { *err = 1; return 0; }
	return *(*p)++;
}

void rdp_clearcodec_init(struct rdp_clearcodec* cc)
{
	memset(cc, 0, sizeof(*cc));
}

int rdp_clearcodec_alloc(struct rdp_clearcodec* cc, const struct rdp_mem* mem)
{
	size_t ng = sizeof(struct rdp_clear_glyph) * RDP_CLEAR_GLYPH_SLOTS;
	size_t nv = sizeof(struct rdp_clear_vbar) * RDP_CLEAR_VBAR_SLOTS;
	size_t ns = sizeof(struct rdp_clear_vbar) * RDP_CLEAR_SHORT_VBAR_SLOTS;
	if (!cc)
		return RDP_CLEAR_E_PROTO;
	if (cc->ready)
		return 0;
	cc->glyph = (struct rdp_clear_glyph*)rdp_mem_alloc(mem, ng);
	cc->vbar = (struct rdp_clear_vbar*)rdp_mem_alloc(mem, nv);
	cc->short_vbar = (struct rdp_clear_vbar*)rdp_mem_alloc(mem, ns);
	if (!cc->glyph || !cc->vbar || !cc->short_vbar)
	{
		rdp_mem_free(mem, cc->glyph);
		rdp_mem_free(mem, cc->vbar);
		rdp_mem_free(mem, cc->short_vbar);
		cc->glyph = NULL;
		cc->vbar = NULL;
		cc->short_vbar = NULL;
		return RDP_CLEAR_E_NOMEM;
	}
	memset(cc->glyph, 0, ng);
	memset(cc->vbar, 0, nv);
	memset(cc->short_vbar, 0, ns);
	cc->ready = 1;
	return 0;
}

static void vbar_free_one(struct rdp_clear_vbar* v, const struct rdp_mem* mem)
{
	rdp_mem_free(mem, v->bgr);
	v->bgr = NULL;
	v->h = 0;
}

static void glyph_free_one(struct rdp_clear_glyph* g, const struct rdp_mem* mem)
{
	rdp_mem_free(mem, g->px);
	g->px = NULL;
	g->n = 0;
}

void rdp_clearcodec_free(struct rdp_clearcodec* cc, const struct rdp_mem* mem)
{
	int i;
	if (!cc)
		return;
	if (cc->glyph)
	{
		for (i = 0; i < RDP_CLEAR_GLYPH_SLOTS; i++)
			glyph_free_one(&cc->glyph[i], mem);
		rdp_mem_free(mem, cc->glyph);
	}
	if (cc->vbar)
	{
		for (i = 0; i < RDP_CLEAR_VBAR_SLOTS; i++)
			vbar_free_one(&cc->vbar[i], mem);
		rdp_mem_free(mem, cc->vbar);
	}
	if (cc->short_vbar)
	{
		for (i = 0; i < RDP_CLEAR_SHORT_VBAR_SLOTS; i++)
			vbar_free_one(&cc->short_vbar[i], mem);
		rdp_mem_free(mem, cc->short_vbar);
	}
	memset(cc, 0, sizeof(*cc));
}

static void reset_cursors(struct rdp_clearcodec* cc)
{
	cc->vbar_cursor = 0;
	cc->short_cursor = 0;
}

static void reset_glyphs(struct rdp_clearcodec* cc, const struct rdp_mem* mem)
{
	int i;
	for (i = 0; i < RDP_CLEAR_GLYPH_SLOTS; i++)
		glyph_free_one(&cc->glyph[i], mem);
}

/* Read residual/RLEX run-length (factor1 / optional 2 / optional 3). */
static int read_run_len(const uint8_t** p, const uint8_t* e, uint32_t* out)
{
	int err = 0;
	uint8_t f1 = rd_u8(p, e, &err);
	uint16_t f2;
	uint32_t f3;
	if (err)
		return -1;
	if (f1 < 0xFF)
	{
		*out = f1;
		return 0;
	}
	f2 = rd_u16(p, e, &err);
	if (err)
		return -1;
	if (f2 < 0xFFFF)
	{
		*out = f2;
		return 0;
	}
	f3 = rd_u32(p, e, &err);
	if (err || f3 == 0)
		return -1;
	*out = f3;
	return 0;
}

static int put_run(uint32_t* dst, uint32_t stride, uint32_t w, uint32_t h,
                   uint32_t* pos, uint32_t color, uint32_t run)
{
	uint32_t i, x, y, max = w * h;
	for (i = 0; i < run; i++)
	{
		if (*pos >= max)
			return -1;
		y = *pos / w;
		x = *pos % w;
		dst[(size_t)y * stride + x] = color;
		(*pos)++;
	}
	return 0;
}

static int decode_residual(const uint8_t* p, size_t len,
                           uint32_t* dst, uint32_t stride, uint32_t w, uint32_t h)
{
	const uint8_t* e = p + len;
	uint32_t pos = 0;
	int err = 0;
	while (p < e)
	{
		uint8_t b = rd_u8(&p, e, &err), g = rd_u8(&p, e, &err), r = rd_u8(&p, e, &err);
		uint32_t run;
		if (err || read_run_len(&p, e, &run) < 0 || run == 0)
			return RDP_CLEAR_E_PROTO;
		if (put_run(dst, stride, w, h, &pos, px_bgr(b, g, r), run) < 0)
			return RDP_CLEAR_E_PROTO;
	}
	return 0;
}

static int vbar_store(struct rdp_clear_vbar* slot, const struct rdp_mem* mem,
                      const uint8_t* bgr, uint8_t h)
{
	uint8_t* n;
	size_t bytes = (size_t)h * 3;
	vbar_free_one(slot, mem);
	if (h == 0)
		return 0;
	n = (uint8_t*)rdp_mem_alloc(mem, bytes);
	if (!n)
		return RDP_CLEAR_E_NOMEM;
	memcpy(n, bgr, bytes);
	slot->bgr = n;
	slot->h = h;
	return 0;
}

static void blit_vbar_col(uint32_t* dst, uint32_t stride, uint32_t x, uint32_t y0,
                          const uint8_t* bgr, uint8_t h)
{
	uint8_t i;
	for (i = 0; i < h; i++)
	{
		const uint8_t* q = bgr + (size_t)i * 3;
		dst[(size_t)(y0 + i) * stride + x] = px_bgr(q[0], q[1], q[2]);
	}
}

static int compose_from_short(uint8_t* out, uint8_t band_h,
                              uint8_t y_on, const uint8_t* short_bgr, uint8_t short_h,
                              uint8_t bkg_b, uint8_t bkg_g, uint8_t bkg_r)
{
	uint8_t y;
	for (y = 0; y < band_h; y++)
	{
		uint8_t* d = out + (size_t)y * 3;
		if (y < y_on || y >= (uint16_t)y_on + short_h)
		{
			d[0] = bkg_b;
			d[1] = bkg_g;
			d[2] = bkg_r;
		}
		else
		{
			const uint8_t* s = short_bgr + (size_t)(y - y_on) * 3;
			d[0] = s[0];
			d[1] = s[1];
			d[2] = s[2];
		}
	}
	return 0;
}

static int decode_bands(struct rdp_clearcodec* cc, const struct rdp_mem* mem,
                        const uint8_t* p, size_t len,
                        uint32_t* dst, uint32_t stride, uint32_t w, uint32_t h)
{
	const uint8_t* e = p + len;
	int err = 0;
	uint8_t tmp[RDP_CLEAR_BAND_MAX_H * 3];
	uint8_t short_tmp[RDP_CLEAR_BAND_MAX_H * 3];

	while (p < e)
	{
		uint16_t x0 = rd_u16(&p, e, &err), x1 = rd_u16(&p, e, &err);
		uint16_t y0 = rd_u16(&p, e, &err), y1 = rd_u16(&p, e, &err);
		uint8_t bb = rd_u8(&p, e, &err), bg = rd_u8(&p, e, &err), br = rd_u8(&p, e, &err);
		uint16_t col, ncols;
		uint8_t band_h;
		if (err || x1 < x0 || y1 < y0 || x1 >= w || y1 >= h)
			return RDP_CLEAR_E_PROTO;
		band_h = (uint8_t)(y1 - y0 + 1);
		if (band_h == 0 || band_h > RDP_CLEAR_BAND_MAX_H)
			return RDP_CLEAR_E_PROTO;
		ncols = (uint16_t)(x1 - x0 + 1);
		for (col = 0; col < ncols; col++)
		{
			uint16_t hdr = rd_u16(&p, e, &err);
			uint16_t x = (uint16_t)(x0 + col);
			if (err)
				return RDP_CLEAR_E_PROTO;
			if (hdr & 0x8000)
			{
				/* VBAR_CACHE_HIT */
				uint16_t idx = (uint16_t)(hdr & 0x7FFF);
				struct rdp_clear_vbar* slot = &cc->vbar[idx];
				if (!slot->bgr || slot->h != band_h)
					return RDP_CLEAR_E_PROTO;
				blit_vbar_col(dst, stride, x, y0, slot->bgr, band_h);
			}
			else if ((hdr & 0xC000) == 0x4000)
			{
				/* SHORT_VBAR_CACHE_HIT */
				uint16_t sidx = (uint16_t)(hdr & 0x3FFF);
				uint8_t y_on = rd_u8(&p, e, &err);
				struct rdp_clear_vbar* ss = &cc->short_vbar[sidx];
				int rc;
				if (err || !ss->bgr || y_on > band_h)
					return RDP_CLEAR_E_PROTO;
				if ((uint16_t)y_on + ss->h > band_h)
					return RDP_CLEAR_E_PROTO;
				compose_from_short(tmp, band_h, y_on, ss->bgr, ss->h, bb, bg, br);
				blit_vbar_col(dst, stride, x, y0, tmp, band_h);
				rc = vbar_store(&cc->vbar[cc->vbar_cursor], mem, tmp, band_h);
				if (rc < 0)
					return rc;
				cc->vbar_cursor = (uint16_t)((cc->vbar_cursor + 1) & 0x7FFF);
			}
			else if ((hdr & 0xC000) == 0)
			{
				/* SHORT_VBAR_CACHE_MISS */
				uint8_t y_on = (uint8_t)(hdr & 0xFF);
				uint8_t y_off = (uint8_t)((hdr >> 8) & 0x3F);
				uint8_t short_h;
				size_t need;
				int rc;
				if (y_off < y_on || y_off > band_h || y_on > band_h)
					return RDP_CLEAR_E_PROTO;
				short_h = (uint8_t)(y_off - y_on);
				need = (size_t)short_h * 3;
				if (p + need > e)
					return RDP_CLEAR_E_PROTO;
				if (short_h)
					memcpy(short_tmp, p, need);
				p += need;
				rc = vbar_store(&cc->short_vbar[cc->short_cursor], mem, short_tmp, short_h);
				if (rc < 0)
					return rc;
				cc->short_cursor = (uint16_t)((cc->short_cursor + 1) & 0x3FFF);
				compose_from_short(tmp, band_h, y_on, short_tmp, short_h, bb, bg, br);
				blit_vbar_col(dst, stride, x, y0, tmp, band_h);
				rc = vbar_store(&cc->vbar[cc->vbar_cursor], mem, tmp, band_h);
				if (rc < 0)
					return rc;
				cc->vbar_cursor = (uint16_t)((cc->vbar_cursor + 1) & 0x7FFF);
			}
			else
				return RDP_CLEAR_E_PROTO;
		}
	}
	return 0;
}

static int floor_log2_p1(unsigned n)
{
	/* floor(log2(n)) + 1 for n >= 1; palette indexes 0..paletteCount-1 */
	int b = 0;
	while (n > 1)
	{
		n >>= 1;
		b++;
	}
	return b + 1;
}

static int decode_rlex(const uint8_t* p, size_t len,
                       uint32_t* dst, uint32_t stride,
                       uint16_t x0, uint16_t y0, uint16_t sw, uint16_t sh)
{
	const uint8_t* e = p + len;
	int err = 0;
	uint8_t palette_count = rd_u8(&p, e, &err);
	uint32_t pal[128];
	int i, index_bits, depth_bits;
	uint32_t pos = 0, total = (uint32_t)sw * sh;
	if (err || palette_count == 0 || palette_count > 0x7F)
		return RDP_CLEAR_E_PROTO;
	for (i = 0; i < palette_count; i++)
	{
		uint8_t b = rd_u8(&p, e, &err), g = rd_u8(&p, e, &err), r = rd_u8(&p, e, &err);
		if (err)
			return RDP_CLEAR_E_PROTO;
		pal[i] = px_bgr(b, g, r);
	}
	/* bits for stopIndex = floor(log2(paletteCount-1))+1 when count>1, else 1? */
	if (palette_count == 1)
		index_bits = 1;
	else
		index_bits = floor_log2_p1((unsigned)(palette_count - 1));
	if (index_bits > 7)
		return RDP_CLEAR_E_PROTO;
	depth_bits = 8 - index_bits;

	while (p < e && pos < total)
	{
		uint8_t packed = rd_u8(&p, e, &err);
		uint8_t stop_index, suite_depth;
		uint32_t run, start_index, j;
		if (err)
			return RDP_CLEAR_E_PROTO;
		stop_index = (uint8_t)(packed & ((1u << index_bits) - 1u));
		suite_depth = (uint8_t)(packed >> index_bits);
		if (depth_bits < 8)
			suite_depth = (uint8_t)(suite_depth & ((1u << depth_bits) - 1u));
		if (stop_index >= palette_count)
			return RDP_CLEAR_E_PROTO;
		if (suite_depth > stop_index)
			return RDP_CLEAR_E_PROTO;
		start_index = (uint32_t)stop_index - suite_depth;
		if (read_run_len(&p, e, &run) < 0)
			return RDP_CLEAR_E_PROTO;
		/* run of startColor, then suite start_index..stop_index inclusive */
		for (j = 0; j < run; j++)
		{
			uint32_t x, y;
			if (pos >= total)
				return RDP_CLEAR_E_PROTO;
			y = pos / sw;
			x = pos % sw;
			dst[(size_t)(y0 + y) * stride + (x0 + x)] = pal[start_index];
			pos++;
		}
		for (j = start_index; j <= stop_index; j++)
		{
			uint32_t x, y;
			if (pos >= total)
				return RDP_CLEAR_E_PROTO;
			y = pos / sw;
			x = pos % sw;
			dst[(size_t)(y0 + y) * stride + (x0 + x)] = pal[j];
			pos++;
		}
	}
	if (pos != total)
		return RDP_CLEAR_E_PROTO;
	return 0;
}

static int decode_subcodecs(struct rdp_clearcodec* cc, const uint8_t* p, size_t len,
                            uint32_t* dst, uint32_t stride, uint32_t w, uint32_t h)
{
	const uint8_t* e = p + len;
	int err = 0;
	while (p < e)
	{
		uint16_t x0 = rd_u16(&p, e, &err), y0 = rd_u16(&p, e, &err);
		uint16_t sw = rd_u16(&p, e, &err), sh = rd_u16(&p, e, &err);
		uint32_t byte_count = rd_u32(&p, e, &err);
		uint8_t id = rd_u8(&p, e, &err);
		const uint8_t* payload;
		uint32_t max_raw;
		int rc;
		if (err || sw == 0 || sh == 0 || (uint32_t)x0 + sw > w || (uint32_t)y0 + sh > h)
			return RDP_CLEAR_E_PROTO;
		max_raw = 3u * (uint32_t)sw * sh;
		if (byte_count > max_raw || p + byte_count > e)
			return RDP_CLEAR_E_PROTO;
		payload = p;
		p += byte_count;
		if (id == RDP_CLEAR_SUB_RAW)
		{
			uint32_t i, n = (uint32_t)sw * sh;
			if (byte_count != max_raw)
				return RDP_CLEAR_E_PROTO;
			for (i = 0; i < n; i++)
			{
				uint32_t x = i % sw, y = i / sw;
				const uint8_t* q = payload + (size_t)i * 3;
				dst[(size_t)(y0 + y) * stride + (x0 + x)] = px_bgr(q[0], q[1], q[2]);
			}
			cc->st.sub_raw++;
		}
		else if (id == RDP_CLEAR_SUB_RLEX)
		{
			rc = decode_rlex(payload, byte_count, dst, stride, x0, y0, sw, sh);
			if (rc < 0)
				return rc;
			cc->st.sub_rlex++;
		}
		else if (id == RDP_CLEAR_SUB_NSC)
		{
			/* not implemented: leave pixels, keep session alive */
			cc->st.nscodec_skip++;
		}
		else
			return RDP_CLEAR_E_PROTO;
	}
	return 0;
}

static int store_glyph(struct rdp_clearcodec* cc, const struct rdp_mem* mem,
                       uint16_t index, const uint32_t* src, uint32_t stride,
                       uint32_t w, uint32_t h)
{
	uint32_t n = w * h, i;
	uint32_t* px;
	if (index >= RDP_CLEAR_GLYPH_SLOTS || n == 0 || n > RDP_CLEAR_GLYPH_MAX_PX)
		return RDP_CLEAR_E_PROTO;
	glyph_free_one(&cc->glyph[index], mem);
	px = (uint32_t*)rdp_mem_alloc(mem, (size_t)n * 4);
	if (!px)
		return RDP_CLEAR_E_NOMEM;
	for (i = 0; i < n; i++)
		px[i] = src[(size_t)(i / w) * stride + (i % w)];
	cc->glyph[index].px = px;
	cc->glyph[index].n = (uint16_t)n;
	cc->st.glyph_store++;
	return 0;
}

static int blit_glyph(const struct rdp_clear_glyph* g,
                      uint32_t* dst, uint32_t stride, uint32_t w, uint32_t h)
{
	uint32_t n = w * h, i;
	if (!g->px || g->n != n)
		return RDP_CLEAR_E_PROTO;
	for (i = 0; i < n; i++)
		dst[(size_t)(i / w) * stride + (i % w)] = g->px[i];
	return 0;
}

int rdp_clearcodec_decode(struct rdp_clearcodec* cc, const struct rdp_mem* mem,
                          const uint8_t* data, size_t len,
                          uint32_t* dst, uint32_t dst_stride,
                          uint32_t dest_w, uint32_t dest_h)
{
	const uint8_t* p = data;
	const uint8_t* e = data + len;
	int err = 0;
	uint8_t flags, seq;
	uint16_t glyph_index = 0;
	uint32_t residual_n, bands_n, sub_n;
	int rc = 0;

	if (!cc || !dst || dest_w == 0 || dest_h == 0 || dest_w > 65535 || dest_h > 65535)
		return RDP_CLEAR_E_PROTO;
	if (!cc->ready)
	{
		int ar = rdp_clearcodec_alloc(cc, mem);
		if (ar < 0)
			return ar;
	}
	if (len < 2)
		return RDP_CLEAR_E_PROTO;

	flags = rd_u8(&p, e, &err);
	seq = rd_u8(&p, e, &err);
	if (err)
		return RDP_CLEAR_E_PROTO;

	if (flags & RDP_CLEAR_FLAG_GLYPH_INDEX)
	{
		glyph_index = rd_u16(&p, e, &err);
		if (err || glyph_index >= RDP_CLEAR_GLYPH_SLOTS)
			return RDP_CLEAR_E_PROTO;
		if (dest_w * dest_h > RDP_CLEAR_GLYPH_MAX_PX)
			return RDP_CLEAR_E_PROTO;
	}
	if ((flags & RDP_CLEAR_FLAG_GLYPH_HIT) && !(flags & RDP_CLEAR_FLAG_GLYPH_INDEX))
		return RDP_CLEAR_E_PROTO;

	/* sequence: first message 0; afterwards previous+1 (wrap 0xFF->0x00) */
	if (!cc->have_seq)
	{
		cc->have_seq = 1;
		cc->expect_seq = seq;
	}
	if (seq != cc->expect_seq)
	{
		cc->st.seq_resync++;
		reset_cursors(cc);
		reset_glyphs(cc, mem);
		cc->expect_seq = seq;
	}
	cc->expect_seq = (uint8_t)(seq + 1);

	if (flags & RDP_CLEAR_FLAG_CACHE_RESET)
	{
		reset_cursors(cc);
		cc->st.cache_reset++;
	}

	if ((flags & RDP_CLEAR_FLAG_GLYPH_INDEX) && (flags & RDP_CLEAR_FLAG_GLYPH_HIT))
	{
		/* composite MUST NOT be present */
		if (p != e)
			return RDP_CLEAR_E_PROTO;
		rc = blit_glyph(&cc->glyph[glyph_index], dst, dst_stride, dest_w, dest_h);
		if (rc < 0)
		{
			cc->st.errors++;
			return rc;
		}
		cc->st.glyph_hit++;
		cc->st.streams++;
		return 0;
	}

	if ((size_t)(e - p) < 12)
		return RDP_CLEAR_E_PROTO;
	residual_n = rd_u32(&p, e, &err);
	bands_n = rd_u32(&p, e, &err);
	sub_n = rd_u32(&p, e, &err);
	if (err || (size_t)residual_n + bands_n + sub_n != (size_t)(e - p))
		return RDP_CLEAR_E_PROTO;

	if (residual_n)
	{
		rc = decode_residual(p, residual_n, dst, dst_stride, dest_w, dest_h);
		if (rc < 0)
			goto fail;
		cc->st.residual++;
		p += residual_n;
	}
	if (bands_n)
	{
		rc = decode_bands(cc, mem, p, bands_n, dst, dst_stride, dest_w, dest_h);
		if (rc < 0)
			goto fail;
		cc->st.bands++;
		p += bands_n;
	}
	if (sub_n)
	{
		rc = decode_subcodecs(cc, p, sub_n, dst, dst_stride, dest_w, dest_h);
		if (rc < 0)
			goto fail;
		p += sub_n;
	}
	(void)p;

	if (flags & RDP_CLEAR_FLAG_GLYPH_INDEX)
	{
		rc = store_glyph(cc, mem, glyph_index, dst, dst_stride, dest_w, dest_h);
		if (rc < 0)
			goto fail;
	}
	cc->st.streams++;
	return 0;

fail:
	cc->st.errors++;
	return rc;
}
