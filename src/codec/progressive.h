/*
 * GFAmigaRDP: the RemoteFX Progressive codec ([MS-RDPEGFX] 2.2.4.2,
 * 3.1.8.1, 3.3.8.2), RDPGFX_CODECID_CAPROGRESSIVE (0x0009) in
 * RDPGFX_WIRE_TO_SURFACE_PDU_2 (2.2.2.2). RLGR1, LL3 deltas, dequantization,
 * the inverse 5/3 DWT and YCbCr to RGB from [MS-RDPRFX] 3.1.8.1.5-3.1.8.2.5;
 * the Reduce-Extrapolate band layout, sub-band diffing, progressive passes
 * (SRL and raw bits) from [MS-RDPEGFX]. Decodes into 0xAARRGGBB surfaces.
 * Written from the public specifications only (CLEAN-ROOM.md).
 * The compute kernels run in OpenGPU when a driver answers with OGPU_ONGPU
 * (struct rdp_prog_accel via ogpu_prog.c); the CPU kernels here are the
 * fallback on a bare 68040 (library FULL without ONGPU).
 * Plain C99. MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_PROGRESSIVE_H
#define RDP_PROGRESSIVE_H

#include <stddef.h>
#include <stdint.h>
#include "gfx_fb.h"

#define RDP_PROG_E_PROTO  (-1)
#define RDP_PROG_E_NOMEM  (-2)

#define RDP_PROG_TILE 64
#define RDP_PROG_COEFS (RDP_PROG_TILE * RDP_PROG_TILE)

/* What a tile keeps between passes (3.3.1.2, 3.3.1.3). coef: DecDwtQ of
 * Y, Cb, Cr (quantized units, 3 x 4096); sign: the tri-state, only while a
 * tile still waits for upgrades; bitpos: the BitPos each band reached. */
struct rdp_prog_tile
{
	int16_t* coef;
	int8_t* sign;
	uint8_t bitpos[3][10];
	uint8_t quant[3][10];        /* the regular factors of the last pass */
	uint8_t re;                  /* Reduce-Extrapolate layout */
	uint8_t painted;             /* 1: pixels written for the current coefs */
};

/* Per surface: a grid of tile states, made as tiles arrive. */
struct rdp_prog_surface
{
	uint16_t gw, gh;
	uint8_t diffing;               /* RFX_PROGRESSIVE_CONTEXT RFX_SUBBAND_DIFFING */
	struct rdp_prog_tile** tiles;  /* gw * gh, NULL until used */
};

struct rdp_prog_stats
{
	uint32_t simple, first, upgrade, skipped, nomem;
	uint32_t view_skip;            /* idwt/YCbCr skipped: tile outside the view clip */
	uint32_t painted;              /* tiles written by rdp_progressive_paint */
	uint32_t trimmed;              /* tiles whose coefs were freed by trim */
	uint32_t accel_tiles;          /* tiles dequantized + DWT + YCbCr by the accel (OpenGPU) */
	uint32_t accel_rlgr;           /* tiles whose RLGR1 the accel decoded */
	uint32_t cpu_fallback;         /* hook calls that said 0 (the CPU did it) */
	uint32_t empty_skip;           /* tile in the view but every stream rect clipped
	                                * away: no dequant/DWT/YCbCr, no accel call */
};

/* Exclusive surface clip for decode/paint; NULL or zero-size = whole surface. */
struct rdp_prog_clip
{
	uint16_t x0, y0, x1, y1;
};

/* Decodes one RFX_PROGRESSIVE_BITMAP_STREAM into px (w x h, 0xAARRGGBB,
 * stride w). on_rect gets each rectangle written (surface coordinates,
 * x1/y1 exclusive). ps may be NULL-initialised (all zero): it grows. */
typedef void (*rdp_prog_rect_fn)(void* user, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
int rdp_progressive_decode(struct rdp_prog_surface* ps, const struct rdp_mem* mem,
                           const uint8_t* data, size_t len, uint32_t* px, uint16_t w, uint16_t h,
                           rdp_prog_rect_fn on_rect, void* user, struct rdp_prog_stats* st,
                           const struct rdp_prog_clip* clip);
/* Re-render stored tiles that intersect [x0,y0)..[x1,y1) into px (after a pan). */
int rdp_progressive_paint(struct rdp_prog_surface* ps, const struct rdp_mem* mem,
                          uint32_t* px, uint16_t w, uint16_t h,
                          uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                          rdp_prog_rect_fn on_rect, void* user, struct rdp_prog_stats* st);
void rdp_progressive_free(struct rdp_prog_surface* ps, const struct rdp_mem* mem);


/* The Progressive compute back end (OpenGPU stream v1.10, src/amiga/ogpu_prog.c).
 * When one is set, entropy decode, dequantize, the inverse DWT and YCbCr to
 * ARGB go to it; this file keeps PDU parsing, tile state, SRL/raw upgrades
 * and the rectangle clip, and its own CPU kernels only for a hook that
 * says 0 (no gfopengpu.library, OGPU_Query NONE, a Submit that failed).
 *
 * rlgr: the RLGR1 of the three components (src[c], len[c] bytes) into
 *   out + c * 4096, with LL3's deltas summed (re: the band layout). 1 done,
 *   0 not taken (the CPU decodes), RDP_PROG_E_PROTO for a malformed stream.
 * tile: dequantize coef (3 x 4096) with quant[c][band], inverse DWT into ycc
 *   (3 x 4096, Y Cb Cr; tmp: RDP_PROG_TMP ints of scratch) and write the
 *   nrects rectangles (x0, y0, x1, y1 exclusive, surface coordinates, already
 *   clipped to the tile, the view and the surface) into px (w wide). The
 *   tile's origin is (tx, ty), nrects >= 1 (a tile with nothing to paint is
 *   counted in empty_skip and never reaches the hook). 1 done, 0 not taken
 *   (the CPU renders all). */
struct rdp_prog_accel
{
	int (*rlgr)(void* user, const uint8_t* const src[3], const uint16_t len[3], int re, int16_t* out);
	int (*tile)(void* user, const int16_t* coef, const uint8_t quant[3][10], int re,
	            int32_t* ycc, int32_t* tmp, uint32_t* px, uint16_t w, int tx, int ty,
	            const uint16_t* rects, int nrects);
	void* user;
};
/* NULL: the in-tree CPU kernels only. The struct is copied. */
void rdp_progressive_set_accel(const struct rdp_prog_accel* a);

#define RDP_PROG_TMP (33 * 33 + 2 * 33 * 64 + 2 * 66 + 64 * 64)

/* Free coef/sign for tiles completely outside keep_*; painted tiles keep their
 * pixels in the surface. Safe under memory pressure / far pans (O1b). */
int rdp_progressive_trim(struct rdp_prog_surface* ps, const struct rdp_mem* mem,
                         uint16_t keep_x0, uint16_t keep_y0, uint16_t keep_x1, uint16_t keep_y1,
                         struct rdp_prog_stats* st);

/* Exposed for the host tests. */
int rdp_rlgr1_decode(const uint8_t* p, size_t len, int16_t* out, int n);
void rdp_prog_idwt(int32_t* buf, int re, int32_t* tmp);
void rdp_prog_band_layout(int re, int band, int* off, int* w, int* h);

#endif
