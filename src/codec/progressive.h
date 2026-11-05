/*
 * GFAmigaRDP: the RemoteFX Progressive codec ([MS-RDPEGFX] 2.2.4.2,
 * 3.1.8.1, 3.3.8.2), RDPGFX_CODECID_CAPROGRESSIVE (0x0009) in
 * RDPGFX_WIRE_TO_SURFACE_PDU_2 (2.2.2.2). RLGR1, LL3 deltas, dequantization,
 * the inverse 5/3 DWT and YCbCr to RGB from [MS-RDPRFX] 3.1.8.1.5-3.1.8.2.5;
 * the Reduce-Extrapolate band layout, sub-band diffing, progressive passes
 * (SRL and raw bits) from [MS-RDPEGFX]. Decodes into 0xAARRGGBB surfaces.
 * Written from the public specifications only (CLEAN-ROOM.md).
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
};

/* Decodes one RFX_PROGRESSIVE_BITMAP_STREAM into px (w x h, 0xAARRGGBB,
 * stride w). on_rect gets each rectangle written (surface coordinates,
 * x1/y1 exclusive). ps may be NULL-initialised (all zero): it grows. */
typedef void (*rdp_prog_rect_fn)(void* user, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
int rdp_progressive_decode(struct rdp_prog_surface* ps, const struct rdp_mem* mem,
                           const uint8_t* data, size_t len, uint32_t* px, uint16_t w, uint16_t h,
                           rdp_prog_rect_fn on_rect, void* user, struct rdp_prog_stats* st);
void rdp_progressive_free(struct rdp_prog_surface* ps, const struct rdp_mem* mem);

/* Exposed for the host tests. */
int rdp_rlgr1_decode(const uint8_t* p, size_t len, int16_t* out, int n);
void rdp_prog_idwt(int32_t* buf, int re, int32_t* tmp);
void rdp_prog_band_layout(int re, int band, int* off, int* w, int* h);

#endif
