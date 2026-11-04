/*
 * GFAmigaRDP: TPKT framing and the X.224 connection request and confirm
 * that open an RDP connection, with RDP's security negotiation inside
 * them. Written from [MS-RDPBCGR] 2.2.1.1 (Client X.224 Connection
 * Request PDU), 2.2.1.2 (Server X.224 Connection Confirm PDU) and the
 * TPKT and X.224 class 0 framing they cite (RFC 1006, ITU-T X.224).
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_X224_H
#define RDP_X224_H

#include <stddef.h>
#include <stdint.h>

/* TPKT: version 3, a reserved byte, the length of the whole packet
 * (header included) big-endian */
#define RDP_TPKT_VERSION 3
#define RDP_TPKT_HEADER 4
#define RDP_TPKT_MAX 0xFFFF

/* X.224 TPDU codes (the high nibble of the code byte) */
#define RDP_X224_CR 0xE0  /* connection request */
#define RDP_X224_CC 0xD0  /* connection confirm */
#define RDP_X224_DT 0xF0  /* data */
#define RDP_X224_DR 0x80  /* disconnect request */

/* requestedProtocols / selectedProtocol ([MS-RDPBCGR] 2.2.1.1.1) */
#define RDP_PROTOCOL_RDP 0x00000000u       /* standard RDP security */
#define RDP_PROTOCOL_SSL 0x00000001u       /* TLS */
#define RDP_PROTOCOL_HYBRID 0x00000002u    /* CredSSP (NLA), over TLS */
#define RDP_PROTOCOL_RDSTLS 0x00000004u
#define RDP_PROTOCOL_HYBRID_EX 0x00000008u /* CredSSP with the early user authorization result */

/* the negotiation structures' type byte */
#define RDP_NEG_REQ 0x01
#define RDP_NEG_RSP 0x02
#define RDP_NEG_FAILURE 0x03

/* RDP_NEG_REQ flags */
#define RDP_NEGREQ_RESTRICTED_ADMIN_MODE_REQUIRED 0x01
#define RDP_NEGREQ_REDIRECTED_AUTHENTICATION_MODE_REQUIRED 0x02
#define RDP_NEGREQ_CORRELATION_INFO_PRESENT 0x08

/* RDP_NEG_RSP flags */
#define RDP_NEGRSP_EXTENDED_CLIENT_DATA_SUPPORTED 0x01
#define RDP_NEGRSP_DYNVC_GFX_PROTOCOL_SUPPORTED 0x02
#define RDP_NEGRSP_RESTRICTED_ADMIN_MODE_SUPPORTED 0x08
#define RDP_NEGRSP_REDIRECTED_AUTHENTICATION_MODE_SUPPORTED 0x10

/* RDP_NEG_FAILURE codes */
#define RDP_NEGFAIL_SSL_REQUIRED_BY_SERVER 1
#define RDP_NEGFAIL_SSL_NOT_ALLOWED_BY_SERVER 2
#define RDP_NEGFAIL_SSL_CERT_NOT_ON_SERVER 3
#define RDP_NEGFAIL_INCONSISTENT_FLAGS 4
#define RDP_NEGFAIL_HYBRID_REQUIRED_BY_SERVER 5
#define RDP_NEGFAIL_SSL_WITH_USER_AUTH_REQUIRED_BY_SERVER 6

/* errors (besides the amipr ones the transport passes on) */
#define RDP_E_PROTO (-100)   /* the server sent something that is not this PDU */
#define RDP_E_SPACE (-101)   /* the buffer is too small */
#define RDP_E_ARG (-102)     /* a bad argument (a cookie with a line break, say) */

/* Builds the Client X.224 Connection Request PDU, TPKT header included.
 * cookie: the user name for "Cookie: mstshash=" (NULL or "": none);
 * protocols: RDP_PROTOCOL_* asked for. With protocols 0 and no flags the
 * RDP_NEG_REQ is still sent (a server that knows it answers with
 * RDP_NEG_RSP). Answers the length, or < 0. */
int rdp_x224_build_cr(uint8_t* buf, size_t size, const char* cookie, uint32_t protocols,
                      uint8_t flags);

/* The confirm, read back */
struct rdp_x224_cc
{
	int has_neg;            /* 1 when the server sent RDP_NEG_RSP or RDP_NEG_FAILURE */
	uint8_t neg_type;       /* RDP_NEG_RSP or RDP_NEG_FAILURE */
	uint8_t neg_flags;
	uint32_t selected;      /* RDP_NEG_RSP: the protocol chosen */
	uint32_t failure;       /* RDP_NEG_FAILURE: RDP_NEGFAIL_* */
};

/* The length a TPKT header gives (4 bytes), or < 0 when it is not one */
int rdp_tpkt_length(const uint8_t hdr[RDP_TPKT_HEADER]);

/* Reads a whole Server X.224 Connection Confirm PDU (TPKT included).
 * 0, or RDP_E_PROTO. A confirm with no negotiation data means the server
 * is an old one that knows only standard RDP security (has_neg 0,
 * selected RDP_PROTOCOL_RDP). */
int rdp_x224_parse_cc(const uint8_t* pdu, size_t len, struct rdp_x224_cc* cc);

/* Words for the user */
const char* rdp_protocol_name(uint32_t protocol);
const char* rdp_negfail_text(uint32_t code);

#endif
