/*
 * GFAmigaRDP: TPKT and the X.224 connection request and confirm.
 * Written from [MS-RDPBCGR] 2.2.1.1 and 2.2.1.2.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 *
 * The request on the wire:
 *   TPKT     03 00 len(BE16)
 *   X.224    LI E0 00 00 00 00 00     (LI: the bytes after LI itself)
 *   cookie   "Cookie: mstshash=<name>" CR LF          (optional)
 *   neg req  01 flags 08 00 protocols(LE32)
 * The confirm is the same up to the code byte (D0), then its own
 * negotiation structure: RSP (02 flags 08 00 selected) or FAILURE
 * (03 flags 08 00 code).
 */
#include "x224.h"
#include "stream.h"

#include <string.h>

#define COOKIE_PREFIX "Cookie: mstshash="
#define NEG_LEN 8
#define COOKIE_MAX 127

int rdp_x224_build_cr(uint8_t* buf, size_t size, const char* cookie, uint32_t protocols,
                      uint8_t flags)
{
	struct rdp_stream s;
	size_t clen = 0, i, total;

	if (!buf)
		return RDP_E_ARG;
	if (cookie)
	{
		clen = strlen(cookie);
		if (clen > COOKIE_MAX)
			return RDP_E_ARG;
		/* the cookie ends at CR LF: it cannot hold one, nor a control character */
		for (i = 0; i < clen; i++)
			if ((unsigned char)cookie[i] < 0x20 || cookie[i] == 0x7F)
				return RDP_E_ARG;
	}
	total = RDP_TPKT_HEADER + 7 + NEG_LEN;
	if (clen)
		total += sizeof(COOKIE_PREFIX) - 1 + clen + 2;
	if (total > size)
		return RDP_E_SPACE;

	rdp_s_init(&s, buf, size);
	rdp_s_u8(&s, RDP_TPKT_VERSION);
	rdp_s_u8(&s, 0);
	rdp_s_u16be(&s, (uint16_t)total);
	rdp_s_u8(&s, (uint8_t)(total - RDP_TPKT_HEADER - 1)); /* LI */
	rdp_s_u8(&s, RDP_X224_CR);
	rdp_s_u16be(&s, 0); /* DST-REF */
	rdp_s_u16be(&s, 0); /* SRC-REF */
	rdp_s_u8(&s, 0);    /* class 0, no options */
	if (clen)
	{
		rdp_s_bytes(&s, COOKIE_PREFIX, sizeof(COOKIE_PREFIX) - 1);
		rdp_s_bytes(&s, cookie, clen);
		rdp_s_u8(&s, '\r');
		rdp_s_u8(&s, '\n');
	}
	rdp_s_u8(&s, RDP_NEG_REQ);
	rdp_s_u8(&s, flags);
	rdp_s_u16le(&s, NEG_LEN);
	rdp_s_u32le(&s, protocols);
	return s.err ? RDP_E_SPACE : (int)s.pos;
}

int rdp_tpkt_length(const uint8_t hdr[RDP_TPKT_HEADER])
{
	int len;

	if (hdr[0] != RDP_TPKT_VERSION)
		return RDP_E_PROTO;
	len = (hdr[2] << 8) | hdr[3];
	return len < RDP_TPKT_HEADER + 3 ? RDP_E_PROTO : len;
}

int rdp_x224_parse_cc(const uint8_t* pdu, size_t len, struct rdp_x224_cc* cc)
{
	struct rdp_stream s;
	int tlen;
	uint8_t li, code;

	if (!pdu || !cc || len < RDP_TPKT_HEADER)
		return RDP_E_PROTO;
	memset(cc, 0, sizeof(*cc));
	cc->selected = RDP_PROTOCOL_RDP;
	tlen = rdp_tpkt_length(pdu);
	if (tlen < 0 || (size_t)tlen != len)
		return RDP_E_PROTO;

	rdp_s_init(&s, (void*)pdu, len);
	rdp_s_skip(&s, RDP_TPKT_HEADER);
	li = rdp_s_get_u8(&s);
	code = rdp_s_get_u8(&s);
	/* LI counts what follows it; it must fit, and hold the 6 fixed bytes */
	if (s.err || (code & 0xF0) != RDP_X224_CC || li < 6 || (size_t)li > len - RDP_TPKT_HEADER - 1)
		return RDP_E_PROTO;
	rdp_s_skip(&s, 5); /* DST-REF, SRC-REF, class */
	if (li == 6)
		return s.err ? RDP_E_PROTO : 0; /* no negotiation data */
	if (li - 6 < NEG_LEN)
		return RDP_E_PROTO;
	cc->neg_type = rdp_s_get_u8(&s);
	cc->neg_flags = rdp_s_get_u8(&s);
	if (rdp_s_get_u16le(&s) != NEG_LEN)
		return RDP_E_PROTO;
	switch (cc->neg_type)
	{
		case RDP_NEG_RSP:
			cc->selected = rdp_s_get_u32le(&s);
			break;
		case RDP_NEG_FAILURE:
			cc->failure = rdp_s_get_u32le(&s);
			break;
		default:
			return RDP_E_PROTO;
	}
	if (s.err)
		return RDP_E_PROTO;
	cc->has_neg = 1;
	return 0;
}

const char* rdp_protocol_name(uint32_t protocol)
{
	switch (protocol)
	{
		case RDP_PROTOCOL_RDP: return "standard RDP security";
		case RDP_PROTOCOL_SSL: return "TLS";
		case RDP_PROTOCOL_HYBRID: return "NLA (CredSSP over TLS)";
		case RDP_PROTOCOL_RDSTLS: return "RDSTLS";
		case RDP_PROTOCOL_HYBRID_EX: return "NLA with early authorization (CredSSP over TLS)";
		default: return "an unknown protocol";
	}
}

const char* rdp_negfail_text(uint32_t code)
{
	switch (code)
	{
		case RDP_NEGFAIL_SSL_REQUIRED_BY_SERVER:
			return "The computer wants TLS.";
		case RDP_NEGFAIL_SSL_NOT_ALLOWED_BY_SERVER:
			return "The computer does not allow TLS.";
		case RDP_NEGFAIL_SSL_CERT_NOT_ON_SERVER:
			return "The computer has no certificate for TLS.";
		case RDP_NEGFAIL_INCONSISTENT_FLAGS:
			return "The computer did not accept the security asked for.";
		case RDP_NEGFAIL_HYBRID_REQUIRED_BY_SERVER:
			return "The computer wants Network Level Authentication.";
		case RDP_NEGFAIL_SSL_WITH_USER_AUTH_REQUIRED_BY_SERVER:
			return "The computer wants TLS with a user certificate.";
		default:
			return "The computer refused the connection.";
	}
}
