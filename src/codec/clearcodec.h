/*
 * GFAmigaRDP: ClearCodec ([MS-RDPEGFX] 2.2.4.1, 3.3.1.9-3.3.1.13, 3.3.8.1),
 * RDPGFX_CODECID_CLEARCODEC (0x0008) in RDPGFX_WIRE_TO_SURFACE_PDU_1.
 * Residual RLE, bands/V-Bars (max height 52), subcodec raw + RLEX; glyph
 * storage 0..3999. NSCodec subcodec (0x01) is counted and skipped.
 * Written from the public specification only (CLEAN-ROOM.md).
 * Plain C99. MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_CLEARCODEC_H
#define RDP_CLEARCODEC_H

#include <stddef.h>
#include <stdint.h>
#include "gfx_fb.h"

#define RDP_CLEAR_E_PROTO   (-1)
#define RDP_CLEAR_E_NOMEM   (-2)

#define RDP_CLEAR_FLAG_GLYPH_INDEX  0x01
#define RDP_CLEAR_FLAG_GLYPH_HIT    0x02
#define RDP_CLEAR_FLAG_CACHE_RESET  0x04

#define RDP_CLEAR_GLYPH_SLOTS       4000
#define RDP_CLEAR_GLYPH_MAX_PX      1024
#define RDP_CLEAR_VBAR_SLOTS        32768
#define RDP_CLEAR_SHORT_VBAR_SLOTS  16384
#define RDP_CLEAR_BAND_MAX_H        52

#define RDP_CLEAR_SUB_RAW   0x00
#define RDP_CLEAR_SUB_NSC   0x01
#define RDP_CLEAR_SUB_RLEX  0x02

struct rdp_clear_glyph
{
	uint16_t n;          /* pixel count; 0: empty */
	uint32_t* px;        /* n x 0xAARRGGBB, linear */
};

struct rdp_clear_vbar
{
	uint8_t h;           /* 0: empty */
	uint8_t* bgr;        /* h x (B,G,R) */
};

struct rdp_clear_stats
{
	uint32_t streams, residual, bands, sub_raw, sub_rlex;
	uint32_t glyph_store, glyph_hit, cache_reset;
	uint32_t nscodec_skip, errors, seq_resync;
};

/* Session ClearCodec state (3.3.1.9-13). Tables are heap-allocated in
 * rdp_clearcodec_alloc so struct rdp_gfx stays small on the 68040. */
struct rdp_clearcodec
{
	uint8_t have_seq, expect_seq, ready;
	uint16_t vbar_cursor;        /* 0..32767 */
	uint16_t short_cursor;       /* 0..16383 */
	struct rdp_clear_glyph* glyph;
	struct rdp_clear_vbar* vbar;
	struct rdp_clear_vbar* short_vbar;
	struct rdp_clear_stats st;
};

void rdp_clearcodec_init(struct rdp_clearcodec* cc);
/* Allocates glyph/V-Bar tables; 0 or RDP_CLEAR_E_NOMEM. */
int rdp_clearcodec_alloc(struct rdp_clearcodec* cc, const struct rdp_mem* mem);
void rdp_clearcodec_free(struct rdp_clearcodec* cc, const struct rdp_mem* mem);

/* Decode one CLEARCODEC_BITMAP_STREAM into dst (dest_w x dest_h of the
 * WireToSurface dest rect, stride = surface stride in pixels). On success
 * the whole dest rect is considered written (caller marks dirty). */
int rdp_clearcodec_decode(struct rdp_clearcodec* cc, const struct rdp_mem* mem,
                          const uint8_t* data, size_t len,
                          uint32_t* dst, uint32_t dst_stride,
                          uint32_t dest_w, uint32_t dest_h);

#endif
