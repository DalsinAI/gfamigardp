/*
 * GFAmigaRDP: the connection sequence ([MS-RDPBCGR] 1.3.1.1). Phase 1 of
 * it so far, the connection initiation: the X.224 request and confirm,
 * then TLS when the server chose TLS or NLA. The MCS Connect Initial with
 * its GCC conference data comes next (docs/DESIGN.md).
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_CONNECT_H
#define RDP_CONNECT_H

#include "transport.h"
#include "x224.h"

struct rdp_settings
{
	const char* host;
	uint16_t port;            /* 3389 */
	const char* user;         /* also the mstshash cookie */
	const char* domain;
	uint32_t protocols;       /* RDP_PROTOCOL_* to offer */
	uint16_t width, height;
	uint8_t depth;            /* 8, 15, 16, 24 */
	int64_t timeout_us;
};

void rdp_settings_default(struct rdp_settings* s);

/* The X.224 exchange on a connected transport: fills cc. 0, or < 0. A
 * RDP_NEG_FAILURE from the server is a success here (cc says why). */
int rdp_negotiate(struct rdp_transport* t, const struct rdp_settings* s, struct rdp_x224_cc* cc);

/* What a failure code means, for the status line */
const char* rdp_error_text(int rc);

#endif
