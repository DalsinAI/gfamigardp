/*
 * GFAmigaRDP: the byte stream under RDP: a TCP connection through
 * bsdsocket.library, upgraded to TLS through gfopentls.library when the
 * negotiation picks TLS or NLA ([MS-RDPBCGR] 5.4.5.1: the TLS handshake
 * starts straight after the X.224 confirm).
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_TRANSPORT_H
#define RDP_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

struct amipr_tls;

struct rdp_transport
{
	int sock;                 /* bsdsocket descriptor, -1 when closed */
	struct amipr_tls* tls;    /* after rdp_transport_start_tls */
	int64_t timeout_us;       /* each wait for the network; < 0 none */
	char host[256];
};

void rdp_transport_init(struct rdp_transport* t);
int rdp_transport_connect(struct rdp_transport* t, const char* host, uint16_t port,
                          int64_t timeout_us);
int rdp_transport_start_tls(struct rdp_transport* t);
/* all len bytes, or < 0 */
int rdp_transport_write(struct rdp_transport* t, const void* buf, size_t len);
/* exactly len bytes, or < 0 (AMIPR_ECLOSED at the end of the stream) */
int rdp_transport_read_full(struct rdp_transport* t, void* buf, size_t len);
/* one TPKT packet, header included: its length, or < 0 */
int rdp_transport_read_tpkt(struct rdp_transport* t, uint8_t* buf, size_t size);
void rdp_transport_close(struct rdp_transport* t);

#endif
