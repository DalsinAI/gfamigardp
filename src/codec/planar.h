/*
 * GFAmigaRDP: the Planar codec, RDP 6.0 Bitmap Compression ([MS-RDPEGDI]
 * 2.2.2.5.1 RDP6_BITMAP_STREAM, 3.1.9), as RDPGFX_CODECID_PLANAR (0x000A)
 * in RDPGFX_WIRE_TO_SURFACE_PDU_1 ([MS-RDPEGFX] 2.2.2.1), and later the
 * 32 bpp legacy bitmap updates. Decodes into 0xAARRGGBB pixels.
 * Plain C99. MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_PLANAR_H
#define RDP_PLANAR_H

#include <stddef.h>
#include <stdint.h>

#define RDP_PLANAR_CLL_MASK 0x07
#define RDP_PLANAR_CS 0x08
#define RDP_PLANAR_RLE 0x10
#define RDP_PLANAR_NA 0x20

#define RDP_PLANAR_E_PROTO (-1)
#define RDP_PLANAR_E_NOMEM (-2)

/* Options */
#define RDP_PLANAR_SWAP_RB 0x01   /* the 3.1.9.1.2 note: legacy 24 bpp YCoCg from Windows servers */

/* One RDP 6.0 RLE plane ([MS-RDPEGDI] 2.2.2.5.1.1, 3.1.9.2.3): w x h bytes
 * into out (w a row). Returns the bytes of src used, or < 0. */
long rdp_planar_rle_plane(const uint8_t* src, size_t len, uint32_t w, uint32_t h, uint8_t* out);

/* A whole stream: w x h pixels into dst (dst_stride pixels a row).
 * scratch: at least rdp_planar_scratch(w, h) bytes, for the planes. */
size_t rdp_planar_scratch(uint32_t w, uint32_t h);
int rdp_planar_decode(const uint8_t* src, size_t len, uint32_t w, uint32_t h,
                      uint32_t* dst, uint32_t dst_stride, uint8_t* scratch, unsigned opts);

/* RDPGFX_CODECID_UNCOMPRESSED (0x0000): w x h 32 bpp pixels, left to right
 * then top to bottom, each B,G,R,XA bytes (RDPGFX_COLOR32 order, 2.2.1.3).
 * has_alpha: ARGB_8888 keeps XA, XRGB_8888 makes it 0xFF. */
int rdp_uncompressed_decode(const uint8_t* src, size_t len, uint32_t w, uint32_t h,
                            uint32_t* dst, uint32_t dst_stride, int has_alpha);

#endif
