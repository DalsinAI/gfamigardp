/*
 * GFAmigaRDP: Network Level Authentication, the CredSSP exchange over the
 * TLS connection when the server chose HYBRID or HYBRID_EX
 * ([MS-RDPBCGR] 5.4.5.2, [MS-CSSP] 3.1.5), through gfopenntlm.library
 * (or libgfopenntlm.a when built with NTLM=static).
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_NLA_H
#define RDP_NLA_H

#include "connect.h"
#include <libraries/gfopenntlm.h>

#define RDP_E_NLA (-110)          /* CredSSP failed: rdp_nla_result says how */

struct rdp_nla_result
{
	long gnerr;                   /* GNERR_* that ended the exchange, 0 on success */
	uint32_t server_error;        /* the server's TSRequest errorCode (an NTSTATUS), 0 if none */
	uint32_t version;             /* CredSSP version used */
	uint32_t peer_version;        /* the server's TSRequest version */
	uint32_t ntlm_peer_version;   /* the server's NTLM Version: major, minor, build */
	int stage;                    /* 1 NEGOTIATE sent, 2 AUTHENTICATE sent, 3 credentials sent */
	int closed_after;             /* the server closed the connection after this stage (0: it did not) */
	uint32_t early_auth;          /* HYBRID_EX: the Early User Authorization Result */
	const char* lib_error;        /* GN_ErrorText of gnerr */
	int bound;                    /* 1: the NTLM exchange carried the TLS channel bindings
	                                 (gfopenntlm.library 1.1 and later, GNA_CHANNELBINDINGS) */
};

/* After rdp_transport_start_tls: authenticate s->user, s->domain and
 * password against the server whose certificate's SubjectPublicKey is
 * pubkey. cb/cb_len: the certificate's tls-server-end-point hash (RFC 5929
 * 4.1, rdp_x509_channel_binding), sent as the NTLM MsvChannelBindings
 * ([MS-NLMP] 2.2.2.1); NULL/0 leaves the exchange unbound. hybrid_ex: the server chose HYBRID_EX (read the Early User
 * Authorization Result too). 0 when the server accepted the credentials;
 * RDP_E_NLA (res says why), AMIPR_ENOLIB (no gfopenntlm.library) or a
 * transport error. The password is never written anywhere but the TLS
 * connection. */
int rdp_nla(struct rdp_transport* t, const struct rdp_settings* s, const char* password,
            const uint8_t* pubkey, size_t pubkey_len, const uint8_t* cb, size_t cb_len,
            int hybrid_ex, struct rdp_nla_result* res);

/* The name of an NTSTATUS or SEC_E_ code ([MS-ERREF] 2.3), or NULL */
const char* rdp_ntstatus_name(uint32_t code);
/* What it means for the user, or NULL */
const char* rdp_ntstatus_text(uint32_t code);

#endif
