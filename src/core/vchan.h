/*
 * GFAmigaRDP: static virtual channel framing ([MS-RDPBCGR] 2.2.6): the
 * CHANNEL_PDU_HEADER and reassembly of chunks up to CHANNEL_CHUNK_LENGTH.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_VCHAN_H
#define RDP_VCHAN_H

#include <stddef.h>
#include <stdint.h>

#define RDP_VC_CHUNK 1600
#define RDP_VC_FLAG_FIRST 0x00000001u
#define RDP_VC_FLAG_LAST  0x00000002u
#define RDP_VC_FLAG_SHOW  0x00000010u

/* One channel's reassembly buffer (opaque payload after the 8-byte header). */
struct rdp_vc_rx
{
	uint8_t* buf;
	size_t size;     /* AllocVec'd capacity */
	size_t have;     /* bytes reassembled so far */
	uint32_t total;  /* CHANNEL_PDU_HEADER::length once FIRST seen */
	int active;      /* 1: a FIRST has come and LAST has not */
};

void rdp_vc_rx_init(struct rdp_vc_rx* r);
void rdp_vc_rx_free(struct rdp_vc_rx* r);

/* Feed one MCS userData that starts with CHANNEL_PDU_HEADER. 1 when a
 * complete message is ready in r->buf (r->have bytes), 0 when more chunks
 * are needed, or < 0 on a protocol error. */
int rdp_vc_rx_feed(struct rdp_vc_rx* r, const uint8_t* data, size_t len);

/* Build one CHANNEL_PDU_HEADER + payload into out (size bytes). Flags
 * FIRST|LAST for a whole message that fits in one chunk. Length written,
 * or < 0 when out is too small. */
int rdp_vc_build(uint8_t* out, size_t size, const uint8_t* payload, size_t len, uint32_t flags);

#endif
