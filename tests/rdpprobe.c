/*
 * GFAmigaRDP: RDPProbe, the connection's first steps from the Shell:
 * TCP to the computer, the X.224 request and confirm, TLS when it was
 * chosen, and with USER and PASSWORD the NLA logon (CredSSP over NTLM,
 * through gfopenntlm.library); prints what the computer chose, its
 * certificate and the logon's result. The password is never printed.
 *
 *   RDPProbe HOST/A PORT/N USER/K DOMAIN/K PASSWORD/K RDP/S TLS/S NLA/S DUMP/K
 *            ASKPASS/S ACTIVATE/S WAIT/N CHANNELS/K EARLY/K DEPTHS/K BPP/N
 * ASKPASS: the password typed at a prompt, not shown (the console in raw
 * mode), rather than on the command line. ACTIVATE: after licensing, the
 * Capabilities Exchange and finalization (session.c), then WAIT seconds
 * (default 5) of what the computer sends, counted; then a disconnect.
 * DUMP=file: the NLA exchange in hex, up to the AUTHENTICATE and the
 * server's answer (never the sealed credentials). The CHALLENGE and
 * AUTHENTICATE together are an NTLMv2 response that can be cracked
 * offline: treat a DUMP file like a password hash, do not share it
 * (review L4).
 * To find what a server wants of the client data (TS_UD_CS_CORE, CS_NET):
 * CHANNELS=drdynvc,cliprdr the static channels offered (default none),
 * EARLY=hex earlyCapabilityFlags (default 0001), DEPTHS=hex
 * supportedColorDepths (default 0007), BPP=8|15|16|24 (default 16).
 * The certificate must chain to a trusted CA and name the host, or be
 * the one trusted before (ENVARC:GFAmigaRDP/KnownHosts, shared with
 * GFAmigaRDP); else RDPProbe prints it and stops, and TRUST saves it
 * there (after you have compared its fingerprint) and connects again.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "connect.h"
#include "amipr.h"
#include "x509.h"
#include "nla.h"
#include "mcs.h"
#include "session.h"
#include "knownhosts.h"
#include "utf16.h"

#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char version[] = "$VER: RDPProbe 0.2 (04.11.2026) AmiCradle Team";

/* the Shell's 4 KB is not enough for a TLS handshake (16 KB) and NLA:
 * libnix's startup swaps to this much (review H2) */
unsigned long __stack = 65536;

#define TEMPLATE "HOST/A,PORT/N,USER/K,DOMAIN/K,PASSWORD/K,RDP/S,TLS/S,NLA/S,DUMP/K,ASKPASS/S,ACTIVATE/S,WAIT/N,CHANNELS/K,EARLY/K,DEPTHS/K,BPP/N,TRUST/S"
enum { A_HOST, A_PORT, A_USER, A_DOMAIN, A_PASSWORD, A_RDP, A_TLS, A_NLA, A_DUMP, A_ASKPASS, A_ACTIVATE, A_WAIT, A_CHANNELS, A_EARLY, A_DEPTHS, A_BPP, A_TRUST, A_N };

static char pw[512];
static char user_utf8[256];
static char domain_utf8[256];

static void wipe(volatile char* p, size_t n)
{
	while (n--)
		*p++ = 0;
}

/* the password from the console with nothing echoed: raw mode, a key at a time */
static int ask_password(void)
{
	BPTR in = Input(), out = Output();
	size_t n = 0;
	char ch;
	if (!IsInteractive(in))
		return 0;
	FPuts(out, (CONST_STRPTR) "Password: ");
	Flush(out);
	SetMode(in, 1);
	while (Read(in, &ch, 1) == 1)
	{
		if (ch == '\r' || ch == '\n')
			break;
		if (ch == 8 || ch == 127)
		{
			if (n) n--;
			continue;
		}
		if (ch == 3 || ch == 27)
		{
			n = 0;
			break;
		}
		if ((unsigned char)ch >= 32 && n + 1 < sizeof(pw))
			pw[n++] = ch;
	}
	SetMode(in, 0);
	pw[n] = 0;
	ch = 0;
	FPuts(out, (CONST_STRPTR) "\n");
	return n > 0;
}

static void print_conn(int rc, const struct rdp_conn* c)
{
	const char* ei = rdp_error_info_text(c->error_info);
	int i;
	if (c->demand.ncaps)
	{
		printf("Demand Active: shareId 0x%08lX, source %u, %u capability sets (mask 0x%08lX), desktop %u x %u at %u bpp",
		       (unsigned long)c->demand.share_id, (unsigned)c->demand.source, (unsigned)c->demand.ncaps,
		       (unsigned long)c->demand.cap_mask, (unsigned)c->demand.width, (unsigned)c->demand.height,
		       (unsigned)c->demand.bpp);
		if (c->demand.session_id)
			printf(", sessionId %lu", (unsigned long)c->demand.session_id);
		printf(".\n");
	}
	printf("Session: %s; %lu PDUs, %lu slow-path updates, fast-path %lu PDUs / %lu updates / %lu bytes.\n",
	       rdp_act_name(c->act), (unsigned long)c->pdus, (unsigned long)c->slow_updates,
	       (unsigned long)c->fp.pdus, (unsigned long)c->fp.updates, (unsigned long)c->fp.bytes);
	if (c->fp.updates)
	{
		printf("Fast-path updateCodes:");
		for (i = 0; i < 16; i++)
			if (c->fp.by_code[i])
				printf(" %d:%lu", i, (unsigned long)c->fp.by_code[i]);
		printf("\n");
	}
	if (c->other_types)
		printf("Slow-path data pduType2 seen: mask 0x%08lX.\n", (unsigned long)c->other_types);
	if (c->error_info)
		printf("Set Error Info: 0x%08lX%s%s.\n", (unsigned long)c->error_info, ei ? " " : "", ei ? ei : "");
	if (c->deactivated_early)
		printf("Deactivate All before any Demand Active: the server dropped the session after licensing\n"
		       "  (MS-RDPBCGR 1.3.1.3). Its own log says why. GNOME Remote Desktop does this unless the\n"
		       "  client data has all three of the Graphics Pipeline (EARLY 0100), a 32 bpp session\n"
		       "  (EARLY 0002, DEPTHS 0008, BPP=24) and the drdynvc channel; to check a server:\n"
		       "  CHANNELS=drdynvc EARLY=0103 DEPTHS=000F BPP=24 (a test only: RDPGFX is not done yet).\n");
	if (c->redirected)
		printf("Server Redirection PDU: the server asked for a reconnection elsewhere (not followed yet).\n");
	if (c->mr.disconnect_reason)
		printf("MCS: Disconnect Provider Ultimatum, reason %u.\n", c->mr.disconnect_reason - 1);
	if (rc < 0)
		printf("Session stopped: %s\n", rdp_error_text(rc));
	{
		unsigned k, first = c->ntrace > 8 ? c->ntrace - 8 : 0;
		for (k = first; k < c->ntrace; k++)
		{
			unsigned i = k % 8, j;
			printf("  PDU %u: %s choice %u channel %u len %u:", k + 1, c->trace[i].fastpath ? "fast-path" : "MCS",
			       (unsigned)c->trace[i].choice, (unsigned)c->trace[i].channel, (unsigned)c->trace[i].len);
			for (j = 0; j < 24; j++)
				printf(" %02X", c->trace[i].head[j]);
			printf("\n");
		}
	}
}

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
	printf("NTLM channel bindings: %s\n", r->bound ? "tls-server-end-point" : "none (gfopenntlm.library 1.0, or no certificate hash)");
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
	static struct rdp_conn conn;   /* its transport is the one connection: TLS keeps a pointer to it */
	struct rdp_transport* t = &conn.t;
	struct rdp_x224_cc cc;
	int rc, retried = 0;

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
	/* Shell / console text is Latin-1; NLA and Client Info want UTF-8 (review L1) */
	if (args[A_USER])
		rdp_amiga_to_utf8(user_utf8, sizeof(user_utf8), (const char*)args[A_USER]);
	if (args[A_DOMAIN])
		rdp_amiga_to_utf8(domain_utf8, sizeof(domain_utf8), (const char*)args[A_DOMAIN]);
	s.user = args[A_USER] ? user_utf8 : NULL;
	s.domain = args[A_DOMAIN] ? domain_utf8 : NULL;
	s.nla_dump = (const char*)args[A_DUMP];
	if (args[A_DUMP])
		printf("DUMP: the file will hold an NTLMv2 response (CHALLENGE + AUTHENTICATE)\n"
		       "      that can be cracked offline. Do not share it.\n");
	if (args[A_RDP] || args[A_TLS] || args[A_NLA])
		s.protocols = (args[A_TLS] ? RDP_PROTOCOL_SSL : 0) |
		              (args[A_NLA] ? RDP_PROTOCOL_HYBRID | RDP_PROTOCOL_HYBRID_EX : 0);
	s.channels = (const char*)args[A_CHANNELS];
	if (args[A_EARLY])
		s.early_caps = (uint16_t)strtoul((const char*)args[A_EARLY], NULL, 16);
	if (args[A_DEPTHS])
		s.color_depths = (uint16_t)strtoul((const char*)args[A_DEPTHS], NULL, 16);
	if (args[A_PASSWORD])
	{
		char raw[256];
		snprintf(raw, sizeof(raw), "%s", (const char*)args[A_PASSWORD]);
		rdp_amiga_to_utf8(pw, sizeof(pw), raw);
		wipe(raw, sizeof(raw));
	}
	else if (args[A_ASKPASS] && ask_password())
	{
		char raw[512];
		memcpy(raw, pw, sizeof(raw));
		rdp_amiga_to_utf8(pw, sizeof(pw), raw);
		wipe(raw, sizeof(raw));
	}

again:
	rdp_conn_init(&conn);
	conn.keyboard_layout = s.keyboard_layout;
	printf("Connecting to %s, port %u...\n", s.host, (unsigned)s.port);
	rc = rdp_transport_connect(t, s.host, s.port, s.timeout_us);
	if (rc == 0)
	{
		printf("Connected. Asking for %s%s%s.\n", s.protocols & RDP_PROTOCOL_SSL ? "TLS" : "",
		       (s.protocols & (RDP_PROTOCOL_SSL | RDP_PROTOCOL_HYBRID)) == (RDP_PROTOCOL_SSL | RDP_PROTOCOL_HYBRID) ? " or " : "",
		       s.protocols & RDP_PROTOCOL_HYBRID ? "NLA" : (s.protocols ? "" : "standard RDP security"));
		rc = rdp_negotiate(t, &s, &cc);
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
				t->have_pin = rdp_known_host_get(s.host, s.port, t->pin);
				rc = rdp_transport_start_tls(t);
				if (rc == AMIPR_ECERT)
				{
					uint8_t fp[32];
					char text[96], name[256];
					const uint8_t* der;
					size_t len;
					struct rdp_x509 x;
					static const char* why[] = { "?", "not signed by a trusted CA (self-signed?)", "for another name",
					                             "expired or not valid yet", "malformed",
					                             "the trust store S:GFOpenTLS is missing", "not the pinned one" };
					int e = amipr_tls_cert_error(t->tls);
					printf("Certificate: %s%s.\n", t->have_pin ? "CHANGED since it was trusted, and " : "",
					       why[e >= 0 && e <= 6 ? e : 0]);
					if (amipr_tls_peer_certificate(t->tls, &der, &len) == 0 && rdp_x509_parse(&x, der, len) == 0)
					{
						if (rdp_x509_name_string(x.subject, x.subject_len, name, sizeof(name)) == 0)
							printf("Subject: %s\n", name);
						printf("Name matches %s: %s\n", s.host, rdp_x509_match_host(&x, s.host) ? "yes" : "NO");
					}
					if (amipr_tls_peer_fingerprint(t->tls, fp) == 0)
					{
						rdp_fingerprint_text(fp, text);
						printf("SHA-256: %s\n", text);
						if (args[A_TRUST] && !retried)
						{
							if (rdp_known_host_set(s.host, s.port, fp) == 0)
							{
								printf("Trusted: saved in %s. Connecting again.\n", rdp_known_hosts_path);
								retried = 1;
								rdp_conn_close(&conn);
								goto again;
							}
							printf("It could not be saved in %s.\n", rdp_known_hosts_path);
						}
						else if (!args[A_TRUST])
							printf("Compare the fingerprint with the computer's own; TRUST trusts it from now on.\n");
					}
					rc = 1;  /* said already */
				}
				else if (rc == 0)
				{
					const uint8_t* der;
					size_t len;
					struct rdp_x509 x;
					char name[256];
					printf("TLS: %s, %s.\n", amipr_tls_protocol(t->tls) == 0x0304 ? "TLS 1.3" : "TLS 1.2",
					       amipr_tls_cipher(t->tls) ? amipr_tls_cipher(t->tls) : "?");
					if (amipr_tls_peer_certificate(t->tls, &der, &len) == 0 && rdp_x509_parse(&x, der, len) == 0 &&
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
						if ((cc.selected & (RDP_PROTOCOL_HYBRID | RDP_PROTOCOL_HYBRID_EX)) && pw[0])
						{
							struct rdp_nla_result nr;
							uint8_t cb[64];  /* tls-server-end-point, up to SHA-512 */
							int cbl = rdp_x509_channel_binding(&x, cb, sizeof(cb), NULL);
							printf("NLA: logging on as %s%s%s (CredSSP over NTLMv2)...\n", s.domain ? s.domain : "",
							       s.domain ? "\\" : "", s.user ? s.user : "");
							rc = rdp_nla(t, &s, pw, x.pubkey, x.pubkey_len, cbl > 0 ? cb : NULL, cbl > 0 ? (size_t)cbl : 0,
							             (cc.selected & RDP_PROTOCOL_HYBRID_EX) != 0, &nr);
							print_nla(rc, &nr);
							if (rc == 0)
							{
								struct rdp_mcs_result mr;
								s.width = 1024;
								s.height = 768;
								s.depth = args[A_BPP] ? (uint8_t)*(LONG*)args[A_BPP] : 16;
								printf("Client data: earlyCapabilityFlags 0x%04X, supportedColorDepths 0x%04X, %u bpp, channels %s.\n",
								       (unsigned)(s.early_caps ? s.early_caps : 1), (unsigned)(s.color_depths ? s.color_depths : 7),
								       (unsigned)s.depth, s.channels && *s.channels ? s.channels : "none");
								rc = rdp_mcs_connect(t, &s, cc.selected, pw, &mr);
								wipe(pw, sizeof(pw));
								print_mcs(rc, &mr);
								if (rc == 0 && args[A_ACTIVATE])
								{
									int secs = args[A_WAIT] ? (int)*(LONG*)args[A_WAIT] : 5, k;
									conn.mr = mr;
									conn.selected = cc.selected;
									conn.width = s.width;
									conn.height = s.height;
									conn.depth = s.depth;
									conn.live = 1;
									printf("Session: Confirm Active and the finalization...\n");
									rc = rdp_conn_activate(&conn, 64);
									print_conn(rc, &conn);
									for (k = 0; rc >= 0 && k < secs * 10; k++)
									{
										Delay(5);
										rc = rdp_conn_poll(&conn, 64);
									}
									if (secs > 0)
									{
										printf("After %d s more:\n", secs);
										print_conn(rc, &conn);
									}
									rdp_conn_close(&conn);
									printf("Disconnected%s.\n", conn.act == RDP_ACT_ENDED ? " (by the computer)" : "");
									rc = rc < 0 ? 1 : 0;
								}
							}
							if (rc != 0)
								rc = 1;  /* said already */
						}
						else if (cc.selected & (RDP_PROTOCOL_HYBRID | RDP_PROTOCOL_HYBRID_EX))
							printf("NLA: give USER and PASSWORD to log on.\n");
					}
				}
				else
					printf("TLS failed: %s\n", t->tls ? amipr_tls_error_text(t->tls) : rdp_error_text(rc));
			}
		}
	}
	if (rc < 0)
		printf("RDPProbe: %s\n", rdp_error_text(rc));
	wipe(pw, sizeof(pw));
	rdp_transport_close(t);
	amipr_net_close();
	FreeArgs(rd);
	return rc ? RETURN_WARN : RETURN_OK;
}
