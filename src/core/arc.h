/*
 * GFAmigaRDP: Automatic Reconnection cookie ([MS-RDPBCGR] 1.3.1.5, 2.2.4.2,
 * 2.2.4.3, 5.5). The Save Session Info PDU carries ARC_SC; the Client Info
 * PDU sends ARC_CS. The password is never kept for reconnect: Enhanced RDP
 * Security uses HMAC-MD5(ArcRandomBits, 32 zero bytes) as the verifier.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_ARC_H
#define RDP_ARC_H

#include <stddef.h>
#include <stdint.h>

#define RDP_ARC_COOKIE_LEN 28
#define RDP_ARC_RANDOM_LEN 16
#define RDP_INFOTYPE_LOGON 0
#define RDP_INFOTYPE_LOGON_LONG 1
#define RDP_INFOTYPE_LOGON_PLAINNOTIFY 2
#define RDP_INFOTYPE_LOGON_EXTENDED_INFO 3
#define RDP_LOGON_EX_AUTORECONNECTCOOKIE 0x00000001u
#define RDP_AUTO_RECONNECT_VERSION_1 0x00000001u

/* Server cookie kept in memory only; never written to disk or favourites. */
struct rdp_arc_cookie
{
	uint32_t logon_id;
	uint8_t random[RDP_ARC_RANDOM_LEN];
	uint8_t have;   /* 1: random + logon_id are valid */
};

void rdp_arc_clear(struct rdp_arc_cookie* c);

/* Parse the body of a Save Session Info data PDU (after the share data
 * header): infoType + infoData. 1 when a cookie was stored (or replaced),
 * 0 when the PDU was another logon type or malformed without a cookie. */
int rdp_arc_from_save_session(struct rdp_arc_cookie* c, const uint8_t* body, size_t len);

/* Build ARC_CS_PRIVATE_PACKET (28 bytes) for Enhanced RDP Security:
 * SecurityVerifier = HMAC-MD5(ArcRandomBits, 32 zero bytes). 0, or < 0. */
int rdp_arc_build_cs(uint8_t out[RDP_ARC_COOKIE_LEN], const struct rdp_arc_cookie* c);

/* Clean-room HMAC-MD5 (RFC 2104 / 2202) for host tests and ARC_CS. */
int rdp_hmac_md5(const uint8_t* key, size_t key_len, const uint8_t* data, size_t data_len,
                 uint8_t out[16]);

#endif
