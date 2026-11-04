/*
 * GFAmigaRDP: RDPProbe, the connection's first steps from the Shell:
 * TCP to the computer, the X.224 request and confirm, and TLS when it
 * was chosen; prints what the computer chose and its certificate.
 *
 *   RDPProbe HOST/A PORT/N USER/K RDP/S TLS/S NLA/S
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "connect.h"
#include "amipr.h"
#include "x509.h"

#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>

static const char version[] = "$VER: RDPProbe 0.1 (04.11.2026) AmiCradle Team";

#define TEMPLATE "HOST/A,PORT/N,USER/K,RDP/S,TLS/S,NLA/S"
enum { A_HOST, A_PORT, A_USER, A_RDP, A_TLS, A_NLA, A_N };

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
					}
				}
				else
					printf("TLS failed: %s\n", t.tls ? amipr_tls_error_text(t.tls) : rdp_error_text(rc));
			}
		}
	}
	if (rc != 0)
		printf("RDPProbe: %s\n", rdp_error_text(rc));
	rdp_transport_close(&t);
	amipr_net_close();
	FreeArgs(rd);
	return rc ? RETURN_WARN : RETURN_OK;
}
