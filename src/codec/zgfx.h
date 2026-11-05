/*
 * GFAmigaRDP: RDP 8.0 Bulk Compression, decompress side ([MS-RDPEGFX]
 * 2.2.5 RDP_SEGMENTED_DATA / RDP_DATA_SEGMENT / RDP8_BULK_ENCODED_DATA and
 * 3.1.9.1). Every server-to-client RDPGFX message comes wrapped in it
 * (2.1). The 2,500,000-byte history ring is kept across calls for the
 * whole channel, so it comes from struct rdp_mem (Fast RAM on the Amiga).
 * Plain C99. MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_ZGFX_H
#define RDP_ZGFX_H

#include <stddef.h>
#include <stdint.h>
#include "gfx_fb.h"

#define RDP_ZGFX_HISTORY 2500000u
#define RDP_ZGFX_SEGMENT_MAX 65535u           /* 3.1.9.1.2.4 */
#define RDP_ZGFX_OUT_MAX (32u * 1024 * 1024)  /* our own cap on one reassembled message */

#define RDP_ZGFX_E_PROTO (-1)
#define RDP_ZGFX_E_NOMEM (-2)

struct rdp_zgfx
{
	struct rdp_mem mem;
	uint8_t* hist;              /* RDP_ZGFX_HISTORY bytes */
	uint32_t hpos;              /* where the next byte goes */
	uint8_t* out;               /* the last result */
	size_t out_len, out_cap;
};

int rdp_zgfx_init(struct rdp_zgfx* z, const struct rdp_mem* mem);
void rdp_zgfx_free(struct rdp_zgfx* z);

/* One RDP_SEGMENTED_DATA. On 0, *out and *out_len hold the decoded bytes,
 * valid until the next call. */
int rdp_zgfx_decompress(struct rdp_zgfx* z, const uint8_t* in, size_t len,
                        const uint8_t** out, size_t* out_len);

#endif
