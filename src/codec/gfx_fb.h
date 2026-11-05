/*
 * GFAmigaRDP: the Graphics Output Buffer ([MS-RDPEGFX] 3.3.1.7) and its
 * dirty rectangles, shared between the connection task (which decodes into
 * it) and the UI task (which blits only what changed, at most once a frame).
 *
 * Pixels are uint32_t 0xAARRGGBB in the CPU's own order. On the 68040 that
 * is A,R,G,B in memory, which WritePixelArray takes as RECTFMT_ARGB with no
 * conversion. All memory comes through struct rdp_mem, so on the Amiga it
 * is AllocVec(MEMF_ANY), which prefers Fast RAM; the default is malloc.
 * The lock hooks are where the UI puts ObtainSemaphore/ReleaseSemaphore on
 * a SignalSemaphore; the default is no lock (host tests, one task).
 * Plain C99. MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_GFX_FB_H
#define RDP_GFX_FB_H

#include <stddef.h>
#include <stdint.h>

struct rdp_mem
{
	void* (*alloc)(void* user, size_t n);   /* NULL: malloc */
	void (*free)(void* user, void* p);      /* NULL: free */
	void* user;
};

void* rdp_mem_alloc(const struct rdp_mem* m, size_t n);
void rdp_mem_free(const struct rdp_mem* m, void* p);

/* exclusive right and bottom, as RDPGFX_RECT16 (2.2.1.2) */
struct rdp_rect
{
	uint16_t x0, y0, x1, y1;
};

#define RDP_DIRTY_MAX 32

/* A small list of changed rectangles. When it fills it folds into one
 * bounding box (overflow = 1), so adding never fails. */
struct rdp_dirty
{
	uint16_t n;
	uint8_t overflow;
	struct rdp_rect r[RDP_DIRTY_MAX];
};

void rdp_dirty_clear(struct rdp_dirty* d);
/* Adds r clipped to w x h; empty rectangles are dropped. */
void rdp_dirty_add(struct rdp_dirty* d, const struct rdp_rect* r, uint16_t w, uint16_t h);
/* The bounding box of all of them; 0 when empty. */
int rdp_dirty_bounds(const struct rdp_dirty* d, struct rdp_rect* out);

struct rdp_fb
{
	uint32_t* px;               /* w x h, stride pixels a row */
	uint16_t w, h;
	uint32_t stride;
	struct rdp_dirty dirty;     /* what the UI has not blitted yet */
	uint32_t frames;            /* frames completed into it */
	struct rdp_mem mem;
	void (*lock)(void* user);   /* NULL: none */
	void (*unlock)(void* user);
	void* lock_user;
};

/* 0, or < 0 when there is no memory. The buffer starts black. */
int rdp_fb_init(struct rdp_fb* fb, const struct rdp_mem* mem, uint16_t w, uint16_t h);
/* A new size (Reset Graphics, 2.2.2.14): black again, all of it dirty. */
int rdp_fb_resize(struct rdp_fb* fb, uint16_t w, uint16_t h);
void rdp_fb_free(struct rdp_fb* fb);
void rdp_fb_lock(struct rdp_fb* fb);
void rdp_fb_unlock(struct rdp_fb* fb);
/* For the UI: copy out and clear the dirty list (under the lock). Returns
 * how many rectangles, and the frame count in *frames when not NULL. */
int rdp_fb_take_dirty(struct rdp_fb* fb, struct rdp_dirty* out, uint32_t* frames);

/* Fill and copy helpers on any 32-bit pixel buffer; rectangles must
 * already be inside both buffers. */
void rdp_px_fill(uint32_t* px, uint32_t stride, const struct rdp_rect* r, uint32_t v);
/* Copies a w x h block; src and dst may overlap (same buffer). */
void rdp_px_copy(uint32_t* dst, uint32_t dstride, int dx, int dy,
                 const uint32_t* src, uint32_t sstride, int sx, int sy, int w, int h);

#endif
