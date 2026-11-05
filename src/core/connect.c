/*
 * GFAmigaRDP: connection initiation ([MS-RDPBCGR] 1.3.1.1, 2.2.1.1-2).
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "connect.h"
#include "mcs.h"
#include "amipr.h"

#include <string.h>

void rdp_settings_default(struct rdp_settings* s)
{
	memset(s, 0, sizeof(*s));
	s->port = 3389;
	/* NLA only by default: with TLS alone the password goes inside the
	 * Client Info PDU, so it is offered only when the user picks it (and
	 * then the certificate must check out; review H1). HYBRID_EX too
	 * ([MS-RDPBCGR] 2.2.1.1.1): a server that has it (Windows 8 / 2012 on)
	 * then says "access denied" before the session starts, in the Early
	 * User Authorization Result, rather than after (nla.c reads it) */
	s->protocols = RDP_PROTOCOL_HYBRID | RDP_PROTOCOL_HYBRID_EX;
	s->width = 800;
	s->height = 600;
	s->depth = 16;
	s->timeout_us = 15000000;
}

int rdp_negotiate(struct rdp_transport* t, const struct rdp_settings* s, struct rdp_x224_cc* cc)
{
	uint8_t buf[512];
	int len, rc;

	len = rdp_x224_build_cr(buf, sizeof(buf), s->user, s->protocols, 0);
	if (len < 0)
		return len;
	rc = rdp_transport_write(t, buf, (size_t)len);
	if (rc < 0)
		return rc;
	len = rdp_transport_read_tpkt(t, buf, sizeof(buf));
	if (len < 0)
		return len;
	rc = rdp_x224_parse_cc(buf, (size_t)len, cc);
	/* no silent downgrade: a protocol the client did not offer (TLS when
	 * only NLA was) is refused, not followed */
	if (rc == 0 && !(cc->has_neg && cc->neg_type == RDP_NEG_FAILURE) && (cc->selected & ~s->protocols))
		return RDP_E_DOWNGRADE;
	return rc;
}

const char* rdp_error_text(int rc)
{
	switch (rc)
	{
		case 0: return "Done.";
		case AMIPR_ENOLIB: return "A library is missing (bsdsocket.library, gfopentls.library).";
		case AMIPR_ENOTFOUND: return "No computer has that name.";
		case AMIPR_ETIMEOUT: return "The computer did not answer in time.";
		case AMIPR_ENET: return "The network refused the connection.";
		case AMIPR_ECLOSED: return "The computer closed the connection.";
		case AMIPR_ETLS: return "The secure connection could not be made.";
		case AMIPR_ECERT: return "The computer's certificate is not one this Amiga trusts.";
		case AMIPR_ECANCEL: return "Cancelled.";
		case RDP_E_UNVERIFIED: return "The password was not sent: the computer's certificate was not checked.";
		case RDP_E_REDIRECT: return "The computer redirected the session to another one, which this version does not follow.";
		case AMIPR_ENOMEM: return "Not enough memory.";
		case RDP_E_PROTO: return "The computer's answer was not RDP.";
		case RDP_E_SPACE: return "The computer's answer was too long.";
		case RDP_E_ARG: return "A setting is not valid.";
		case RDP_E_DOWNGRADE: return "The computer chose a weaker security than was asked for; not connected.";
		case RDP_E_MCS: return "The computer refused an MCS step.";
		case RDP_E_LICENSE: return "The computer asked for a licence exchange, which this version does not do yet.";
		default: return "The connection failed.";
	}
}
