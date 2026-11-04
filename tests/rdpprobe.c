/*
 * GFAmigaRDP: RDPProbe, the connection's first steps from the Shell:
 * TCP to the computer, the X.224 request and confirm, TLS when it was
 * chosen, and with USER and PASSWORD the NLA logon (CredSSP over NTLM,
 * through gfopenntlm.library); prints what the computer chose, its
 * certificate and the logon's result. The password is never printed.
 *
 *   RDPProbe HOST/A PORT/N USER/K DOMAIN/K PASSWORD/K RDP/S TLS/S NLA/S DUMP/K
 * DUMP=file: the NLA exchange in hex, up to the AUTHENTICATE and the
 * server's answer (never the credentials), to check a logon offline.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "connect.h"
#include "amipr.h"
#include "x509.h"
#include "nla.h"
#include "mcs.h"

#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>

static const char version[] = "$VER: RDPProbe 0.2 (04.11.2026) AmiCradle Team";

#define TEMPLATE "HOST/A,PORT/N,USER/K,DOMAIN/K,PASSWORD/K,RDP/S,TLS/S,NLA/S,DUMP/K"
enum { A_HOST, A_PORT, A_USER, A_DOMAIN, A_PASSWORD, A_RDP, A_TLS, A_NLA, A_DUMP, A_N };

/* "YYMMDDhhmmssZ" (UTCTime, RFC 5280: YY < 50 is 20YY) or "YYYYMMDDhhmmssZ"
 * (GeneralizedTime) as "YYYY-MM-DD hh:mm:ss UTC"; anything else as it is */
static const char* cert_time(const char* t, char out[24])
{
	size_t n = strlen(t);
	char y[5];
	if (n == 13 && t[12] == 'Z')
	{
		snprintf(y, sizeof(y), "%s%.2s", t[0] < '5' ? "20" : "19", t);
		t += 2;
	}
	else if (n == 15 && t[14] == 'Z')
	{
		snprintf(y, sizeof(y), "%.4s", t);
		t += 4;
	}
	else
		return t;
	snprintf(out, 24, "%s-%.2s-%.2s %.2s:%.2s:%.2s UTC", y, t, t + 2, t + 4, t + 6, t + 8);
	return out;
}

static const char* stage_name(int stage)
{
	switch (stage)
	{
		case 1: return "the NTLM NEGOTIATE";
		case 2: return "the NTLM AUTHENTICATE";
		case 3: return "the credentials";
		default: return "the start";
	}
}

static void print_mcs(int rc, const struct rdp_mcs_result* m)
{
	int i;
	if (m->stage >= RDP_MCS_ST_CONNECTED)
	{
		printf("MCS: connected. Server RDP version 0x%08lX, encryption method %lu level %lu, I/O channel %u",
		       (unsigned long)m->server_version, (unsigned long)m->enc_method, (unsigned long)m->enc_level,
		       (unsigned)m->io_channel);
		if (m->msg_channel)
			printf(", message channel %u", (unsigned)m->msg_channel);
		for (i = 0; i < m->channel_count; i++)
			printf("%s%u", i ? " " : ", static channels ", (unsigned)m->channels[i]);
		printf(".\n");
	}
	if (m->stage >= RDP_MCS_ST_ATTACHED)
		printf("MCS: Erect Domain sent; Attach User confirmed, user channel %u.\n", (unsigned)m->user_channel);
	if (m->stage >= RDP_MCS_ST_JOINED)
		printf("MCS: every channel joined.\n");
	if (m->stage >= RDP_MCS_ST_INFO_SENT)
		printf("Client Info PDU sent (no Security Exchange: TLS carries it).\n");
	if (m->stage >= RDP_MCS_ST_LICENSING)
	{
		printf("Licensing: security flags 0x%04X, bMsgType 0x%02X, flags 0x%02X, wMsgSize %u",
		       (unsigned)m->sec_flags, (unsigned)m->lic_type, (unsigned)m->lic_flags, (unsigned)m->lic_size);
		if (m->lic_type == 0xFF)
			printf(": dwErrorCode 0x%08lX %s, dwStateTransition %lu", (unsigned long)m->lic_error,
			       rdp_license_error_text(m->lic_error), (unsigned long)m->lic_state);
		printf(".\n");
		if (m->lic_type == 0xFF && m->lic_error == 7)
			printf("Reached licensing: the server says the client's licence is valid; next comes Demand Active.\n");
		else if (m->lic_type != 0xFF)
			printf("Reached licensing: the server starts a licence exchange ([MS-RDPELE]); stopping here.\n");
		return;
	}
	if (m->connect_result)
		printf("MCS: the server refused the Connect Initial: result %u.\n", m->connect_result);
	else if (m->attach_result)
		printf("MCS: Attach User refused: result %u.\n", m->attach_result);
	else if (m->join_result)
		printf("MCS: Channel Join of %u refused: result %u.\n", (unsigned)m->join_failed, m->join_result);
	if (m->disconnect_reason)
		printf("MCS: Disconnect Provider Ultimatum, reason %u.\n", m->disconnect_reason - 1);
	if (m->error_info)
		printf("Set Error Info: 0x%08lX.\n", (unsigned long)m->error_info);
	if (m->stage == RDP_MCS_ST_CONNECTED && m->enc_level)
		printf("MCS: the server wants standard RDP encryption inside TLS; not supported.\n");
	printf("Stopped after %s: %s\n", rdp_mcs_stage_name(m->stage), rc ? rdp_error_text(rc) : "?");
}

static void print_nla(int rc, const struct rdp_nla_result* r)
{
	if (r->ntlm_peer_version)
		printf("Server NTLM version: %u.%u build %u\n", (unsigned)(r->ntlm_peer_version & 0xff),
		       (unsigned)((r->ntlm_peer_version >> 8) & 0xff), (unsigned)(r->ntlm_peer_version >> 16));
	if (r->peer_version)
		printf("CredSSP: version %lu (the server's %lu)\n", (unsigned long)r->version, (unsigned long)r->peer_version);
	if (rc == 0)
	{
		printf("NLA: the server accepted the credentials.\n");
		return;
	}
	if (rc == RDP_E_NLA && r->gnerr == GNERR_SERVER)
	{
		const char* name = rdp_ntstatus_name(r->server_error);
		const char* text = rdp_ntstatus_text(r->server_error);
		printf("NLA: the server refused after %s: errorCode 0x%08lX%s%s%s%s\n", stage_name(r->stage),
		       (unsigned long)r->server_error, name ? " " : "", name ? name : "", text ? ": " : "", text ? text : "");
		return;
	}
	if (rc == RDP_E_NLA && r->early_auth)
	{
		printf("NLA: Early User Authorization Result 0x%08lX%s\n", (unsigned long)r->early_auth,
		       r->early_auth == 5 ? " ACCESS_DENIED" : "");
		return;
	}
	if (rc == RDP_E_NLA)
	{
		printf("NLA failed after %s: %s (%ld)\n", stage_name(r->stage), r->lib_error ? r->lib_error : "?", r->gnerr);
		return;
	}
	if (r->closed_after)
	{
		printf("NLA: the server closed the connection after %s, with no errorCode%s\n", stage_name(r->closed_after),
		       r->closed_after == 2 ? " (how a server without CredSSP errorCode refuses a logon)" : "");
		return;
	}
	if (rc == AMIPR_ENOLIB)
		printf("NLA: gfopenntlm.library %d is not in LIBS:\n", GFOPENNTLMLIB_VERSION);
	else
		printf("NLA failed after %s: %s\n", stage_name(r->stage), rdp_error_text(rc));
}

int main(void)
{
	LONG args[A_N] = { 0 };
	struct RDArgs* rd;
	struct rdp_settings s;
	struct rdp_transport t;
	struct rdp_x224_cc cc;
	int rc;

	(void)version;
	rd = ReadArgs((CONST_STRPTR)TEMPLATE, args, NULL);
	if (!rd)
	{
		PrintFault(IoErr(), (CONST_STRPTR) "RDPProbe");
		return RETURN_ERROR;
	}
	if (amipr_init() != AMIPR_OK)
	{
		printf("RDPProbe: timer.device could not be opened\n");
		FreeArgs(rd);
		return RETURN_FAIL;
	}
	rdp_settings_default(&s);
	s.host = (const char*)args[A_HOST];
	if (args[A_PORT])
		s.port = (uint16_t) * (LONG*)args[A_PORT];
	s.user = (const char*)args[A_USER];
	s.domain = (const char*)args[A_DOMAIN];
	s.nla_dump = (const char*)args[A_DUMP];
	if (args[A_RDP] || args[A_TLS] || args[A_NLA])
		s.protocols = (args[A_TLS] ? RDP_PROTOCOL_SSL : 0) | (args[A_NLA] ? RDP_PROTOCOL_HYBRID : 0);

	rdp_transport_init(&t);
	printf("Connecting to %s, port %u...\n", s.host, (unsigned)s.port);
	rc = rdp_transport_connect(&t, s.host, s.port, s.timeout_us);
	if (rc == 0)
	{
		printf("Connected. Asking for %s%s%s.\n", s.protocols & RDP_PROTOCOL_SSL ? "TLS" : "",
		       (s.protocols & (RDP_PROTOCOL_SSL | RDP_PROTOCOL_HYBRID)) == (RDP_PROTOCOL_SSL | RDP_PROTOCOL_HYBRID) ? " or " : "",
		       s.protocols & RDP_PROTOCOL_HYBRID ? "NLA" : (s.protocols ? "" : "standard RDP security"));
		rc = rdp_negotiate(&t, &s, &cc);
	}
	if (rc == 0)
	{
		if (cc.has_neg && cc.neg_type == RDP_NEG_FAILURE)
			printf("The computer said no (code %lu): %s\n", (unsigned long)cc.failure, rdp_negfail_text(cc.failure));
		else
		{
			printf("The computer chose %s (selectedProtocol 0x%08lx, flags 0x%02x)%s.\n",
			       rdp_protocol_name(cc.selected), (unsigned long)cc.selected, cc.neg_flags,
			       cc.has_neg ? "" : ", with no negotiation data: an old server");
			if (cc.selected & (RDP_PROTOCOL_SSL | RDP_PROTOCOL_HYBRID | RDP_PROTOCOL_HYBRID_EX))
			{
				rc = rdp_transport_start_tls(&t);
				if (rc == 0)
				{
					const uint8_t* der;
					size_t len;
					struct rdp_x509 x;
					char name[256];
					printf("TLS: %s, %s.\n", amipr_tls_protocol(t.tls) == 0x0304 ? "TLS 1.3" : "TLS 1.2",
					       amipr_tls_cipher(t.tls) ? amipr_tls_cipher(t.tls) : "?");
					if (amipr_tls_peer_certificate(t.tls, &der, &len) == 0 && rdp_x509_parse(&x, der, len) == 0 &&
					    rdp_x509_name_string(x.subject, x.subject_len, name, sizeof(name)) == 0)
					{
						uint8_t fp[32];
						struct amipr_hash h;
						int i;
						char from[24], to[24];
						printf("Certificate: %s (%lu-bit %s key)\n", name, (unsigned long)x.key_bits, x.key_rsa ? "RSA" : "non-RSA");
						if (rdp_x509_name_string(x.issuer, x.issuer_len, name, sizeof(name)) == 0)
							printf("Issued by: %s%s\n", name,
							       x.issuer_len == x.subject_len && memcmp(x.issuer, x.subject, x.subject_len) == 0 ? " (self-signed)" : "");
						printf("Valid: %s to %s\n", cert_time(x.not_before, from), cert_time(x.not_after, to));
						amipr_hash_init(&h, AMIPR_SHA256);
						amipr_hash_update(&h, der, len);
						amipr_hash_final(&h, fp, sizeof(fp));
						printf("SHA-256:");
						for (i = 0; i < 32; i++)
							printf("%s%02X", i ? ":" : " ", fp[i]);
						printf("\n");
						if ((cc.selected & (RDP_PROTOCOL_HYBRID | RDP_PROTOCOL_HYBRID_EX)) && args[A_PASSWORD])
						{
							struct rdp_nla_result nr;
							printf("NLA: logging on as %s%s%s (CredSSP over NTLMv2)...\n", s.domain ? s.domain : "",
							       s.domain ? "\\" : "", s.user ? s.user : "");
							rc = rdp_nla(&t, &s, (const char*)args[A_PASSWORD], x.pubkey, x.pubkey_len,
							             (cc.selected & RDP_PROTOCOL_HYBRID_EX) != 0, &nr);
							print_nla(rc, &nr);
							if (rc == 0)
							{
								struct rdp_mcs_result mr;
								rc = rdp_mcs_connect(&t, &s, cc.selected, (const char*)args[A_PASSWORD], &mr);
								print_mcs(rc, &mr);
							}
							if (rc != 0)
								rc = 1;  /* said already */
						}
						else if (cc.selected & (RDP_PROTOCOL_HYBRID | RDP_PROTOCOL_HYBRID_EX))
							printf("NLA: give USER and PASSWORD to log on.\n");
					}
				}
				else
					printf("TLS failed: %s\n", t.tls ? amipr_tls_error_text(t.tls) : rdp_error_text(rc));
			}
		}
	}
	if (rc < 0)
		printf("RDPProbe: %s\n", rdp_error_text(rc));
	rdp_transport_close(&t);
	amipr_net_close();
	FreeArgs(rd);
	return rc ? RETURN_WARN : RETURN_OK;
}
