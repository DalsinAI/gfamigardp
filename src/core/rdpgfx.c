/*
 * GFAmigaRDP: the Graphics Pipeline client (rdpgfx.h). [MS-RDPEGFX] 2.2.1
 * (header, rectangles, points, colours), 2.2.2 (PDUs), 2.2.3 (capability
 * sets), 3.1.5.1 (header checks), 3.3.1 (surfaces, cache, output buffer,
 * mapping) and 3.3.5 (processing each PDU, frame acknowledge). Plain C99.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include <string.h>
#include "rdpgfx.h"
#include "planar.h"
#include "stream.h"

#define HDR 8

/* What we offer, newest first. Every 10.x has AVC_DISABLED (without it
 * the client claims AVC444, 2.2.3.x); 8.1 never sets AVC420_ENABLED; 10.1
 * has no flags so it is left out; 10.3 has no SMALL_CACHE. */
static const struct
{
	uint32_t version, flags;
} offer[] = {
	{ RDPGFX_CAPVERSION_107, RDPGFX_CAPS_FLAG_SMALL_CACHE | RDPGFX_CAPS_FLAG_AVC_DISABLED | RDPGFX_CAPS_FLAG_SCALEDMAP_DISABLE },
	{ RDPGFX_CAPVERSION_106, RDPGFX_CAPS_FLAG_SMALL_CACHE | RDPGFX_CAPS_FLAG_AVC_DISABLED },
	{ RDPGFX_CAPVERSION_105, RDPGFX_CAPS_FLAG_SMALL_CACHE | RDPGFX_CAPS_FLAG_AVC_DISABLED },
	{ RDPGFX_CAPVERSION_104, RDPGFX_CAPS_FLAG_SMALL_CACHE | RDPGFX_CAPS_FLAG_AVC_DISABLED },
	{ RDPGFX_CAPVERSION_103, RDPGFX_CAPS_FLAG_AVC_DISABLED },
	{ RDPGFX_CAPVERSION_102, RDPGFX_CAPS_FLAG_SMALL_CACHE | RDPGFX_CAPS_FLAG_AVC_DISABLED },
	{ RDPGFX_CAPVERSION_10, RDPGFX_CAPS_FLAG_SMALL_CACHE | RDPGFX_CAPS_FLAG_AVC_DISABLED },
	{ RDPGFX_CAPVERSION_81, RDPGFX_CAPS_FLAG_SMALL_CACHE },
	{ RDPGFX_CAPVERSION_8, RDPGFX_CAPS_FLAG_SMALL_CACHE },
};
#define NOFFER (sizeof(offer) / sizeof(offer[0]))

static void header(struct rdp_stream* s, uint16_t cmd, uint32_t len)
{
	rdp_s_u16le(s, cmd);
	rdp_s_u16le(s, 0);
	rdp_s_u32le(s, len);
}

size_t rdp_gfx_build_caps(uint8_t* buf, size_t size)
{
	return rdp_gfx_build_caps_mask(buf, size, 0);
}

size_t rdp_gfx_build_caps_mask(uint8_t* buf, size_t size, uint16_t mask)
{
	struct rdp_stream s;
	size_t i, n = 0, len;
	if (!(mask & ((1u << NOFFER) - 1)))
		mask = (uint16_t)((1u << NOFFER) - 1);
	for (i = 0; i < NOFFER; i++)
		if (mask & (1u << i))
			n++;
	len = HDR + 2 + n * 12;
	rdp_s_init(&s, buf, size);
	header(&s, RDPGFX_CMDID_CAPSADVERTISE, (uint32_t)len);
	rdp_s_u16le(&s, (uint16_t)n);
	for (i = 0; i < NOFFER; i++)
	{
		if (!(mask & (1u << i)))
			continue;
		rdp_s_u32le(&s, offer[i].version);
		rdp_s_u32le(&s, 4);
		rdp_s_u32le(&s, offer[i].flags);
	}
	return s.err ? 0 : s.pos;
}

int rdp_gfx_init(struct rdp_gfx* g, const struct rdp_mem* mem, uint16_t w, uint16_t h,
                 rdp_gfx_send_fn send, void* user)
{
	size_t n = sizeof(struct rdp_gfx_cache_entry) * RDP_GFX_CACHE_SLOTS;
	memset(g, 0, sizeof(*g));
	if (mem)
		g->mem = *mem;
	g->send = send;
	g->send_user = user;
	if (rdp_fb_init(&g->fb, &g->mem, w, h) < 0)
		return RDP_GFX_E_NOMEM;
	if (rdp_zgfx_init(&g->zgfx, &g->mem) < 0)
	{
		rdp_gfx_free(g);
		return RDP_GFX_E_NOMEM;
	}
	g->cache = (struct rdp_gfx_cache_entry*)rdp_mem_alloc(&g->mem, n);
	if (!g->cache)
	{
		rdp_gfx_free(g);
		return RDP_GFX_E_NOMEM;
	}
	memset(g->cache, 0, n);
	return 0;
}

static void surface_delete(struct rdp_gfx* g, struct rdp_gfx_surface* sf)
{
	rdp_progressive_free(&sf->prog, &g->mem);
	rdp_mem_free(&g->mem, sf->px);
	memset(sf, 0, sizeof(*sf));
}

static void cache_evict(struct rdp_gfx* g, uint16_t slot)
{
	struct rdp_gfx_cache_entry* e = &g->cache[slot - 1];
	if (e->px)
	{
		g->cache_bytes -= (size_t)e->w * e->h * 4;
		rdp_mem_free(&g->mem, e->px);
	}
	memset(e, 0, sizeof(*e));
}

void rdp_gfx_free(struct rdp_gfx* g)
{
	int i;
	for (i = 0; i < RDP_GFX_MAX_SURFACES; i++)
		if (g->surf[i].used)
			surface_delete(g, &g->surf[i]);
	if (g->cache)
	{
		for (i = 1; i <= RDP_GFX_CACHE_SLOTS; i++)
			cache_evict(g, (uint16_t)i);
		rdp_mem_free(&g->mem, g->cache);
		g->cache = NULL;
	}
	rdp_mem_free(&g->mem, g->scratch);
	g->scratch = NULL;
	rdp_zgfx_free(&g->zgfx);
	rdp_fb_free(&g->fb);
}

static int send_pdu(struct rdp_gfx* g, const uint8_t* p, size_t n)
{
	if (!g->send)
		return 0;
	return g->send(g->send_user, p, n) < 0 ? RDP_GFX_E_SEND : 0;
}

int rdp_gfx_on_open(struct rdp_gfx* g)
{
	uint8_t buf[HDR + 2 + NOFFER * 12];
	size_t n = rdp_gfx_build_caps_mask(buf, sizeof(buf), g->offer_mask);
	return n ? send_pdu(g, buf, n) : RDP_GFX_E_PROTO;
}

static struct rdp_gfx_surface* surface_find(struct rdp_gfx* g, uint16_t id)
{
	int i;
	for (i = 0; i < RDP_GFX_MAX_SURFACES; i++)
		if (g->surf[i].used && g->surf[i].id == id)
			return &g->surf[i];
	return NULL;
}

static void get_rect(struct rdp_stream* s, struct rdp_rect* r)
{
	r->x0 = rdp_s_get_u16le(s);
	r->y0 = rdp_s_get_u16le(s);
	r->x1 = rdp_s_get_u16le(s);
	r->y1 = rdp_s_get_u16le(s);
}

/* clip r to the surface; 0 when nothing is left */
static int clip(struct rdp_rect* r, uint16_t w, uint16_t h)
{
	if (r->x1 > w) r->x1 = w;
	if (r->y1 > h) r->y1 = h;
	return r->x0 < r->x1 && r->y0 < r->y1;
}

static uint32_t color32(struct rdp_stream* s, uint8_t fmt)
{
	uint8_t b = rdp_s_get_u8(s), gg = rdp_s_get_u8(s), r = rdp_s_get_u8(s), a = rdp_s_get_u8(s);
	if (fmt != GFX_PIXEL_FORMAT_ARGB_8888)
		a = 0xFF;
	return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)gg << 8) | b;
}

static void mark(struct rdp_gfx_surface* sf, const struct rdp_rect* r)
{
	rdp_dirty_add(&sf->dirty, r, sf->w, sf->h);
}

static int send_frame_ack(struct rdp_gfx* g, uint32_t frame_id)
{
	uint8_t buf[HDR + 12];
	struct rdp_stream s;
	rdp_s_init(&s, buf, sizeof(buf));
	header(&s, RDPGFX_CMDID_FRAMEACKNOWLEDGE, sizeof(buf));
	/* One frame in flight: a slow 68040 asks the server to ease off
	 * ([MS-RDPEGFX] 2.2.2.13) instead of QUEUE_DEPTH_UNAVAILABLE. */
	rdp_s_u32le(&s, 1);
	rdp_s_u32le(&s, frame_id);
	rdp_s_u32le(&s, g->frames_decoded);
	return send_pdu(g, buf, s.pos);
}

void rdp_gfx_flush(struct rdp_gfx* g)
{
	int i;
	uint16_t k;
	rdp_fb_lock(&g->fb);
	for (i = 0; i < RDP_GFX_MAX_SURFACES; i++)
	{
		struct rdp_gfx_surface* sf = &g->surf[i];
		if (!sf->used || !sf->dirty.n)
			continue;
		if (sf->mapped)
		{
			for (k = 0; k < sf->dirty.n; k++)
			{
				const struct rdp_rect* r = &sf->dirty.r[k];
				uint32_t dx = sf->ox + r->x0, dy = sf->oy + r->y0;
				uint32_t w = (uint32_t)r->x1 - r->x0, h = (uint32_t)r->y1 - r->y0;
				struct rdp_rect o;
				if (dx >= g->fb.w || dy >= g->fb.h)
					continue;
				if (dx + w > g->fb.w) w = g->fb.w - dx;
				if (dy + h > g->fb.h) h = g->fb.h - dy;
				rdp_px_copy(g->fb.px, g->fb.stride, (int)dx, (int)dy, sf->px, sf->w, r->x0, r->y0,
				            (int)w, (int)h);
				o.x0 = (uint16_t)dx;
				o.y0 = (uint16_t)dy;
				o.x1 = (uint16_t)(dx + w);
				o.y1 = (uint16_t)(dy + h);
				rdp_dirty_add(&g->fb.dirty, &o, g->fb.w, g->fb.h);
			}
		}
		rdp_dirty_clear(&sf->dirty);
	}
	rdp_fb_unlock(&g->fb);
}

static int scratch(struct rdp_gfx* g, size_t n)
{
	if (n <= g->scratch_cap)
		return 0;
	rdp_mem_free(&g->mem, g->scratch);
	g->scratch_cap = 0;
	g->scratch = (uint8_t*)rdp_mem_alloc(&g->mem, n);
	if (!g->scratch)
		return RDP_GFX_E_NOMEM;
	g->scratch_cap = n;
	return 0;
}

static void prog_rect(void* user, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	struct rdp_rect r;
	r.x0 = x0;
	r.y0 = y0;
	r.x1 = x1;
	r.y1 = y1;
	mark((struct rdp_gfx_surface*)user, &r);
}

/* RDPGFX_WIRE_TO_SURFACE_PDU_2 (2.2.2.2): RemoteFX Progressive. A bad
 * stream is counted and skipped; the session goes on. */
static int wire_to_surface_2(struct rdp_gfx* g, struct rdp_stream* s)
{
	uint16_t id = rdp_s_get_u16le(s), codec = rdp_s_get_u16le(s);
	uint32_t ctx = rdp_s_get_u32le(s), len;
	uint8_t fmt = rdp_s_get_u8(s);
	struct rdp_gfx_surface* sf;
	int rc;
	(void)ctx;
	(void)fmt;
	len = rdp_s_get_u32le(s);
	if (s->err || rdp_s_left(s) < len)
		return RDP_GFX_E_PROTO;
	if (codec < 16)
		g->st.codec_count[codec]++;
	if (codec != RDPGFX_CODECID_CAPROGRESSIVE || !(sf = surface_find(g, id)))
	{
		g->st.skipped_codec++;
		g->st.last_skipped_codec = codec;
		rdp_s_skip(s, len);
		return 0;
	}
	{
		struct rdp_prog_clip clip, *pc = NULL;
		if (g->view_set && sf->mapped)
		{
			int32_t x0 = (int32_t)g->view_x0 - (int32_t)sf->ox;
			int32_t y0 = (int32_t)g->view_y0 - (int32_t)sf->oy;
			int32_t x1 = (int32_t)g->view_x1 - (int32_t)sf->ox;
			int32_t y1 = (int32_t)g->view_y1 - (int32_t)sf->oy;
			if (x0 < 0) x0 = 0;
			if (y0 < 0) y0 = 0;
			if (x1 > sf->w) x1 = sf->w;
			if (y1 > sf->h) y1 = sf->h;
			if (x1 > x0 && y1 > y0)
			{
				clip.x0 = (uint16_t)x0;
				clip.y0 = (uint16_t)y0;
				clip.x1 = (uint16_t)x1;
				clip.y1 = (uint16_t)y1;
				pc = &clip;
			}
		}
		rc = rdp_progressive_decode(&sf->prog, &g->mem, s->p + s->pos, len, sf->px, sf->w, sf->h,
		                            prog_rect, sf, &g->st.prog, pc);
	}
	rdp_s_skip(s, len);
	if (rc < 0)
		g->st.prog_errors++;
	else
		g->st.progressive++;
	return 0;
}

static int wire_to_surface_1(struct rdp_gfx* g, struct rdp_stream* s)
{
	uint16_t id = rdp_s_get_u16le(s), codec = rdp_s_get_u16le(s);
	uint8_t fmt = rdp_s_get_u8(s);
	struct rdp_rect r;
	uint32_t len, w, h;
	const uint8_t* data;
	struct rdp_gfx_surface* sf;
	uint32_t* dst;
	int rc;

	get_rect(s, &r);
	len = rdp_s_get_u32le(s);
	if (s->err || rdp_s_left(s) < len)
		return RDP_GFX_E_PROTO;
	data = s->p + s->pos;
	rdp_s_skip(s, len);
	(void)fmt;
	if (codec < 16)
		g->st.codec_count[codec]++;
	if (codec == RDPGFX_CODECID_AVC420 || codec == RDPGFX_CODECID_AVC444 || codec == RDPGFX_CODECID_AVC444v2)
	{
		/* we never offered it; skip rather than tear the session down */
		g->st.avc_seen++;
		return 0;
	}
	if (!(sf = surface_find(g, id)) || r.x0 >= r.x1 || r.y0 >= r.y1 || r.x1 > sf->w || r.y1 > sf->h)
		return RDP_GFX_E_PROTO;
	w = (uint32_t)r.x1 - r.x0;
	h = (uint32_t)r.y1 - r.y0;
	dst = sf->px + (size_t)r.y0 * sf->w + r.x0;
	switch (codec)
	{
	case RDPGFX_CODECID_UNCOMPRESSED:
		rc = rdp_uncompressed_decode(data, len, w, h, dst, sf->w, sf->fmt == GFX_PIXEL_FORMAT_ARGB_8888);
		g->st.uncompressed++;
		break;
	case RDPGFX_CODECID_PLANAR:
		if ((rc = scratch(g, rdp_planar_scratch(w, h))) < 0)
			return rc;
		rc = rdp_planar_decode(data, len, w, h, dst, sf->w, g->scratch, 0);
		g->st.planar++;
		break;
	default:
		g->st.skipped_codec++;
		g->st.last_skipped_codec = codec;
		return 0;
	}
	if (rc < 0)
		return RDP_GFX_E_PROTO;
	mark(sf, &r);
	return 0;
}

static int solid_fill(struct rdp_gfx* g, struct rdp_stream* s)
{
	uint16_t id = rdp_s_get_u16le(s), n, i;
	struct rdp_gfx_surface* sf = surface_find(g, id);
	uint32_t c;
	if (!sf)
		return RDP_GFX_E_PROTO;
	c = color32(s, sf->fmt);
	n = rdp_s_get_u16le(s);
	for (i = 0; i < n && !s->err; i++)
	{
		struct rdp_rect r;
		get_rect(s, &r);
		if (!s->err && clip(&r, sf->w, sf->h))
		{
			rdp_px_fill(sf->px, sf->w, &r, c);
			mark(sf, &r);
		}
	}
	g->st.solid_fills++;
	return s->err ? RDP_GFX_E_PROTO : 0;
}

static int surface_to_surface(struct rdp_gfx* g, struct rdp_stream* s)
{
	uint16_t sid = rdp_s_get_u16le(s), did = rdp_s_get_u16le(s), n, i;
	struct rdp_gfx_surface *src = surface_find(g, sid), *dst = surface_find(g, did);
	struct rdp_rect r;
	get_rect(s, &r);
	n = rdp_s_get_u16le(s);
	if (!src || !dst || s->err)
		return RDP_GFX_E_PROTO;
	if (!clip(&r, src->w, src->h))
	{
		rdp_s_skip(s, (size_t)n * 4);
		return s->err ? RDP_GFX_E_PROTO : 0;
	}
	for (i = 0; i < n && !s->err; i++)
	{
		int16_t x = (int16_t)rdp_s_get_u16le(s), y = (int16_t)rdp_s_get_u16le(s);
		int w = r.x1 - r.x0, h = r.y1 - r.y0, sx = r.x0, sy = r.y0;
		struct rdp_rect d;
		if (s->err)
			break;
		/* clip the destination to the target surface, moving the source with it */
		if (x < 0) { w += x; sx -= x; x = 0; }
		if (y < 0) { h += y; sy -= y; y = 0; }
		if (x + w > dst->w) w = dst->w - x;
		if (y + h > dst->h) h = dst->h - y;
		if (w <= 0 || h <= 0)
			continue;
		rdp_px_copy(dst->px, dst->w, x, y, src->px, src->w, sx, sy, w, h);
		d.x0 = (uint16_t)x;
		d.y0 = (uint16_t)y;
		d.x1 = (uint16_t)(x + w);
		d.y1 = (uint16_t)(y + h);
		mark(dst, &d);
	}
	g->st.s2s++;
	return s->err ? RDP_GFX_E_PROTO : 0;
}

static int surface_to_cache(struct rdp_gfx* g, struct rdp_stream* s)
{
	uint16_t id = rdp_s_get_u16le(s), slot;
	struct rdp_gfx_surface* sf = surface_find(g, id);
	struct rdp_gfx_cache_entry* e;
	struct rdp_rect r;
	size_t bytes;
	rdp_s_skip(s, 8);   /* cacheKey: only for persistent caching, which we do not offer */
	slot = rdp_s_get_u16le(s);
	get_rect(s, &r);
	if (s->err || !sf || slot < 1 || slot > RDP_GFX_CACHE_SLOTS || !clip(&r, sf->w, sf->h))
		return RDP_GFX_E_PROTO;
	cache_evict(g, slot);
	e = &g->cache[slot - 1];
	e->w = (uint16_t)(r.x1 - r.x0);
	e->h = (uint16_t)(r.y1 - r.y0);
	bytes = (size_t)e->w * e->h * 4;
	if (g->cache_bytes + bytes > RDP_GFX_CACHE_BYTES)
		return RDP_GFX_E_PROTO;   /* 3.3.1.4: the server must keep within it */
	e->px = (uint32_t*)rdp_mem_alloc(&g->mem, bytes);
	if (!e->px)
		return RDP_GFX_E_NOMEM;
	g->cache_bytes += bytes;
	rdp_px_copy(e->px, e->w, 0, 0, sf->px, sf->w, r.x0, r.y0, e->w, e->h);
	g->st.s2c++;
	return 0;
}

static int cache_to_surface(struct rdp_gfx* g, struct rdp_stream* s)
{
	uint16_t slot = rdp_s_get_u16le(s), id = rdp_s_get_u16le(s), n, i;
	struct rdp_gfx_surface* sf = surface_find(g, id);
	struct rdp_gfx_cache_entry* e;
	n = rdp_s_get_u16le(s);
	if (s->err || !sf || slot < 1 || slot > RDP_GFX_CACHE_SLOTS || !g->cache[slot - 1].px)
		return RDP_GFX_E_PROTO;
	e = &g->cache[slot - 1];
	for (i = 0; i < n && !s->err; i++)
	{
		int16_t x = (int16_t)rdp_s_get_u16le(s), y = (int16_t)rdp_s_get_u16le(s);
		int w = e->w, h = e->h, sx = 0, sy = 0;
		struct rdp_rect d;
		if (s->err)
			break;
		if (x < 0) { w += x; sx -= x; x = 0; }
		if (y < 0) { h += y; sy -= y; y = 0; }
		if (x + w > sf->w) w = sf->w - x;
		if (y + h > sf->h) h = sf->h - y;
		if (w <= 0 || h <= 0)
			continue;
		rdp_px_copy(sf->px, sf->w, x, y, e->px, e->w, sx, sy, w, h);
		d.x0 = (uint16_t)x;
		d.y0 = (uint16_t)y;
		d.x1 = (uint16_t)(x + w);
		d.y1 = (uint16_t)(y + h);
		mark(sf, &d);
	}
	g->st.c2s++;
	return s->err ? RDP_GFX_E_PROTO : 0;
}

static int create_surface(struct rdp_gfx* g, struct rdp_stream* s)
{
	uint16_t id = rdp_s_get_u16le(s), w = rdp_s_get_u16le(s), h = rdp_s_get_u16le(s);
	uint8_t fmt = rdp_s_get_u8(s);
	struct rdp_gfx_surface* sf = NULL;
	size_t n;
	int i;
	if (s->err || !w || !h || (fmt != GFX_PIXEL_FORMAT_XRGB_8888 && fmt != GFX_PIXEL_FORMAT_ARGB_8888))
		return RDP_GFX_E_PROTO;
	if ((sf = surface_find(g, id)) != NULL)
		surface_delete(g, sf);
	for (i = 0; i < RDP_GFX_MAX_SURFACES && !sf; i++)
		if (!g->surf[i].used)
			sf = &g->surf[i];
	if (!sf)
		return RDP_GFX_E_NOMEM;
	n = (size_t)w * h * 4;
	sf->px = (uint32_t*)rdp_mem_alloc(&g->mem, n);
	if (!sf->px)
		return RDP_GFX_E_NOMEM;
	memset(sf->px, 0, n);   /* 3.3.5.10 leaves the contents undefined; black is kind */
	sf->used = 1;
	sf->id = id;
	sf->w = w;
	sf->h = h;
	sf->fmt = fmt;
	return 0;
}

static int handle(struct rdp_gfx* g, uint16_t cmd, struct rdp_stream* s)
{
	switch (cmd)
	{
	case RDPGFX_CMDID_CAPSCONFIRM:
	{
		uint32_t v = rdp_s_get_u32le(s), len = rdp_s_get_u32le(s), flags = 0;
		size_t i;
		if (len >= 4)
			flags = rdp_s_get_u32le(s);
		if (s->err)
			return RDP_GFX_E_PROTO;
		for (i = 0; i < NOFFER; i++)
			if (offer[i].version == v)
				break;
		if (i == NOFFER)
			return RDP_GFX_E_PROTO;   /* not one we offered */
		g->caps_version = v;
		g->caps_flags = flags;
		g->caps_ok = 1;
		return 0;
	}
	case RDPGFX_CMDID_RESETGRAPHICS:
	{
		uint32_t w = rdp_s_get_u32le(s), h = rdp_s_get_u32le(s), mons = rdp_s_get_u32le(s);
		if (s->err || !w || !h || w > 32766 || h > 32766 || mons > 16)
			return RDP_GFX_E_PROTO;
		return rdp_fb_resize(&g->fb, (uint16_t)w, (uint16_t)h) < 0 ? RDP_GFX_E_NOMEM : 0;
	}
	case RDPGFX_CMDID_CREATESURFACE:
		return create_surface(g, s);
	case RDPGFX_CMDID_DELETESURFACE:
	{
		struct rdp_gfx_surface* sf = surface_find(g, rdp_s_get_u16le(s));
		if (sf)
			surface_delete(g, sf);
		return 0;
	}
	case RDPGFX_CMDID_MAPSURFACETOOUTPUT:
	{
		struct rdp_gfx_surface* sf = surface_find(g, rdp_s_get_u16le(s));
		struct rdp_rect all;
		rdp_s_skip(s, 2);
		if (!sf)
			return RDP_GFX_E_PROTO;
		sf->ox = rdp_s_get_u32le(s);
		sf->oy = rdp_s_get_u32le(s);
		sf->mapped = 1;
		all.x0 = all.y0 = 0;
		all.x1 = sf->w;
		all.y1 = sf->h;
		mark(sf, &all);
		return s->err ? RDP_GFX_E_PROTO : 0;
	}
	case RDPGFX_CMDID_STARTFRAME:
		rdp_s_skip(s, 4);   /* timestamp */
		g->frame_id = rdp_s_get_u32le(s);
		g->in_frame = 1;
		return s->err ? RDP_GFX_E_PROTO : 0;
	case RDPGFX_CMDID_ENDFRAME:
	{
		uint32_t id = rdp_s_get_u32le(s);
		if (s->err)
			return RDP_GFX_E_PROTO;
		g->in_frame = 0;
		g->frames_decoded++;
		g->st.frames++;
		rdp_gfx_flush(g);
		rdp_fb_lock(&g->fb);
		g->fb.frames++;
		rdp_fb_unlock(&g->fb);
		return send_frame_ack(g, id);
	}
	case RDPGFX_CMDID_SOLIDFILL:
		return solid_fill(g, s);
	case RDPGFX_CMDID_SURFACETOSURFACE:
		return surface_to_surface(g, s);
	case RDPGFX_CMDID_SURFACETOCACHE:
		return surface_to_cache(g, s);
	case RDPGFX_CMDID_CACHETOSURFACE:
		return cache_to_surface(g, s);
	case RDPGFX_CMDID_EVICTCACHEENTRY:
	{
		uint16_t slot = rdp_s_get_u16le(s);
		if (s->err || slot < 1 || slot > RDP_GFX_CACHE_SLOTS)
			return RDP_GFX_E_PROTO;
		cache_evict(g, slot);
		return 0;
	}
	case RDPGFX_CMDID_WIRETOSURFACE_1:
		return wire_to_surface_1(g, s);
	case RDPGFX_CMDID_WIRETOSURFACE_2:
		return wire_to_surface_2(g, s);
	case RDPGFX_CMDID_DELETEENCODINGCONTEXT:
	case RDPGFX_CMDID_CACHEIMPORTREPLY:   /* we send no offer, so nothing is imported */
	case RDPGFX_CMDID_MAPSURFACETOWINDOW:
	case RDPGFX_CMDID_MAPSURFACETOSCALEDOUTPUT:
	case RDPGFX_CMDID_MAPSURFACETOSCALEDWINDOW:
	default:
		return 0;   /* unknown ids are ignored (3.1.5.1 allows it for forward compatibility) */
	}
}

int rdp_gfx_process_pdus(struct rdp_gfx* g, const uint8_t* p, size_t len)
{
	size_t pos = 0;
	while (len - pos >= HDR)
	{
		struct rdp_stream s;
		uint16_t cmd = (uint16_t)(p[pos] | (p[pos + 1] << 8));
		uint32_t plen = (uint32_t)p[pos + 4] | ((uint32_t)p[pos + 5] << 8) | ((uint32_t)p[pos + 6] << 16) |
		                ((uint32_t)p[pos + 7] << 24);
		int rc;
		if (plen < HDR || plen > len - pos)
		{
			g->st.errors++;
			return RDP_GFX_E_PROTO;
		}
		rdp_s_init(&s, (uint8_t*)(p + pos + HDR), plen - HDR);
		rc = handle(g, cmd, &s);
		g->st.pdus++;
		if (rc < 0)
		{
			g->st.errors++;
			return rc;
		}
		pos += plen;
	}
	if (pos != len)
	{
		g->st.errors++;
		return RDP_GFX_E_PROTO;
	}
	return 0;
}

int rdp_gfx_input(struct rdp_gfx* g, const uint8_t* data, size_t len)
{
	const uint8_t* out;
	size_t n;
	int rc;
	g->st.bytes_in += (uint32_t)len;
	if (rdp_zgfx_decompress(&g->zgfx, data, len, &out, &n) < 0)
	{
		g->st.errors++;
		return RDP_GFX_E_PROTO;
	}
	rc = rdp_gfx_process_pdus(g, out, n);
	if (rc == 0 && !g->in_frame)
		rdp_gfx_flush(g);   /* anything drawn outside a frame (Map, for one) */
	return rc;
}

void rdp_gfx_set_view(struct rdp_gfx* g, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	if (!g)
		return;
	if (x1 <= x0 || y1 <= y0)
	{
		g->view_set = 0;
		g->view_x0 = g->view_y0 = g->view_x1 = g->view_y1 = 0;
		return;
	}
	if (x1 > g->fb.w) x1 = g->fb.w;
	if (y1 > g->fb.h) y1 = g->fb.h;
	g->view_set = 1;
	g->view_x0 = x0;
	g->view_y0 = y0;
	g->view_x1 = x1;
	g->view_y1 = y1;
}

int rdp_gfx_paint_view(struct rdp_gfx* g)
{
	int i, n = 0;
	if (!g || !g->view_set)
		return 0;
	for (i = 0; i < RDP_GFX_MAX_SURFACES; i++)
	{
		struct rdp_gfx_surface* sf = &g->surf[i];
		int32_t x0, y0, x1, y1;
		int rc;
		if (!sf->used || !sf->mapped || !sf->prog.tiles)
			continue;
		x0 = (int32_t)g->view_x0 - (int32_t)sf->ox;
		y0 = (int32_t)g->view_y0 - (int32_t)sf->oy;
		x1 = (int32_t)g->view_x1 - (int32_t)sf->ox;
		y1 = (int32_t)g->view_y1 - (int32_t)sf->oy;
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 > sf->w) x1 = sf->w;
		if (y1 > sf->h) y1 = sf->h;
		if (x1 <= x0 || y1 <= y0)
			continue;
		rc = rdp_progressive_paint(&sf->prog, &g->mem, sf->px, sf->w, sf->h,
		                           (uint16_t)x0, (uint16_t)y0, (uint16_t)x1, (uint16_t)y1,
		                           prog_rect, sf, &g->st.prog);
		if (rc > 0)
			n += rc;
	}
	if (n)
		rdp_gfx_flush(g);
	return n;
}
