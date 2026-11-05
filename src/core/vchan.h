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

/* The largest message the client will buffer, static channel or dynamic
 * (review 2 N5): the server names the size (CHANNEL_PDU_HEADER.length, a
 * DVC DATA_FIRST's Length), up to 4 GB; a bigger message is read past
 * and dropped, and the session goes on. */
#define RDP_VC_MAX_MESSAGE (4UL * 1024 * 1024)

/* One channel's reassembly buffer (opaque payload after the 8-byte header). */
struct rdp_vc_rx
{
	uint8_t* buf;
	size_t size;     /* AllocVec'd capacity */
	size_t have;     /* bytes reassembled so far */
	uint32_t total;  /* CHANNEL_PDU_HEADER::length once FIRST seen */
	int active;      /* 1: a FIRST has come and LAST has not */
	int skip;        /* 1: this message is over RDP_VC_MAX_MESSAGE: read past, not kept */
	uint32_t dropped; /* messages dropped for their size */
};

void rdp_vc_rx_init(struct rdp_vc_rx* r);
void rdp_vc_rx_free(struct rdp_vc_rx* r);

/* Feed one MCS userData that starts with CHANNEL_PDU_HEADER. 1 when a
 * complete message is ready in r->buf (r->have bytes), 0 when more chunks
 * are needed or the message was over RDP_VC_MAX_MESSAGE and dropped
 * (r->dropped), or < 0 on a protocol error. */
int rdp_vc_rx_feed(struct rdp_vc_rx* r, const uint8_t* data, size_t len);

/* Build one CHANNEL_PDU_HEADER + payload into out (size bytes). Flags
 * FIRST|LAST for a whole message that fits in one chunk. Length written,
 * or < 0 when out is too small. */
int rdp_vc_build(uint8_t* out, size_t size, const uint8_t* payload, size_t len, uint32_t flags);

#endif
