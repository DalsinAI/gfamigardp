/*
 * GFAmigaRDP: the Graphics Output Buffer and dirty rectangles (gfx_fb.h).
 * [MS-RDPEGFX] 3.3.1.7. Plain C99.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include <stdlib.h>
#include <string.h>
#include "gfx_fb.h"

void* rdp_mem_alloc(const struct rdp_mem* m, size_t n)
{
	if (m && m->alloc)
		return m->alloc(m->user, n);
	return malloc(n ? n : 1);
}

void rdp_mem_free(const struct rdp_mem* m, void* p)
{
	if (!p)
		return;
	if (m && m->free)
		m->free(m->user, p);
	else
		free(p);
}

void rdp_dirty_clear(struct rdp_dirty* d)
{
	d->n = 0;
	d->overflow = 0;
}

static int overlaps_or_touches(const struct rdp_rect* a, const struct rdp_rect* b)
{
	return a->x0 <= b->x1 && b->x0 <= a->x1 && a->y0 <= b->y1 && b->y0 <= a->y1;
}

static void unite(struct rdp_rect* a, const struct rdp_rect* b)
{
	if (b->x0 < a->x0) a->x0 = b->x0;
	if (b->y0 < a->y0) a->y0 = b->y0;
	if (b->x1 > a->x1) a->x1 = b->x1;
	if (b->y1 > a->y1) a->y1 = b->y1;
}

void rdp_dirty_add(struct rdp_dirty* d, const struct rdp_rect* in, uint16_t w, uint16_t h)
{
	struct rdp_rect r = *in;
	uint16_t i;
	if (r.x1 > w) r.x1 = w;
	if (r.y1 > h) r.y1 = h;
	if (r.x0 >= r.x1 || r.y0 >= r.y1)
		return;
	/* merge into a rectangle it overlaps or touches; that keeps the list
	 * short for the common case of tiles side by side */
	for (i = 0; i < d->n; i++)
	{
		if (overlaps_or_touches(&d->r[i], &r))
		{
			unite(&d->r[i], &r);
			return;
		}
	}
	if (d->n < RDP_DIRTY_MAX)
	{
		d->r[d->n++] = r;
		return;
	}
	/* full: fold everything into one box */
	for (i = 1; i < d->n; i++)
		unite(&d->r[0], &d->r[i]);
	unite(&d->r[0], &r);
	d->n = 1;
	d->overflow = 1;
}

int rdp_dirty_bounds(const struct rdp_dirty* d, struct rdp_rect* out)
{
	uint16_t i;
	if (!d->n)
		return 0;
	*out = d->r[0];
	for (i = 1; i < d->n; i++)
		unite(out, &d->r[i]);
	return 1;
}

static int fb_alloc(struct rdp_fb* fb, uint16_t w, uint16_t h)
{
	size_t n = (size_t)w * h * 4;
	fb->px = (uint32_t*)rdp_mem_alloc(&fb->mem, n);
	if (!fb->px)
		return -1;
	memset(fb->px, 0, n);
	fb->w = w;
	fb->h = h;
	fb->stride = w;
	return 0;
}

int rdp_fb_init(struct rdp_fb* fb, const struct rdp_mem* mem, uint16_t w, uint16_t h)
{
	memset(fb, 0, sizeof(*fb));
	if (mem)
		fb->mem = *mem;
	rdp_dirty_clear(&fb->dirty);
	return fb_alloc(fb, w, h);
}

int rdp_fb_resize(struct rdp_fb* fb, uint16_t w, uint16_t h)
{
	struct rdp_rect all;
	int rc;
	rdp_fb_lock(fb);
	rdp_mem_free(&fb->mem, fb->px);
	fb->px = NULL;
	fb->w = fb->h = 0;
	rc = fb_alloc(fb, w, h);
	rdp_dirty_clear(&fb->dirty);
	if (rc == 0)
	{
		all.x0 = all.y0 = 0;
		all.x1 = w;
		all.y1 = h;
		rdp_dirty_add(&fb->dirty, &all, w, h);
	}
	rdp_fb_unlock(fb);
	return rc;
}

void rdp_fb_free(struct rdp_fb* fb)
{
	rdp_mem_free(&fb->mem, fb->px);
	fb->px = NULL;
	fb->w = fb->h = 0;
}

void rdp_fb_lock(struct rdp_fb* fb)
{
	if (fb->lock)
		fb->lock(fb->lock_user);
}

void rdp_fb_unlock(struct rdp_fb* fb)
{
	if (fb->unlock)
		fb->unlock(fb->lock_user);
}

int rdp_fb_take_dirty(struct rdp_fb* fb, struct rdp_dirty* out, uint32_t* frames)
{
	int n;
	rdp_fb_lock(fb);
	*out = fb->dirty;
	n = fb->dirty.n;
	if (frames)
		*frames = fb->frames;
	rdp_dirty_clear(&fb->dirty);
	rdp_fb_unlock(fb);
	return n;
}

void rdp_px_fill(uint32_t* px, uint32_t stride, const struct rdp_rect* r, uint32_t v)
{
	uint32_t y, x;
	for (y = r->y0; y < r->y1; y++)
	{
		uint32_t* row = px + (size_t)y * stride;
		for (x = r->x0; x < r->x1; x++)
			row[x] = v;
	}
}

void rdp_px_copy(uint32_t* dst, uint32_t dstride, int dx, int dy,
                 const uint32_t* src, uint32_t sstride, int sx, int sy, int w, int h)
{
	int y;
	if (w <= 0 || h <= 0)
		return;
	if (dst == src && dy > sy)
	{
		/* moving down inside one buffer: bottom row first */
		for (y = h - 1; y >= 0; y--)
			memmove(dst + (size_t)(dy + y) * dstride + dx, src + (size_t)(sy + y) * sstride + sx,
			        (size_t)w * 4);
		return;
	}
	for (y = 0; y < h; y++)
		memmove(dst + (size_t)(dy + y) * dstride + dx, src + (size_t)(sy + y) * sstride + sx,
		        (size_t)w * 4);
}
