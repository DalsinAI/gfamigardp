/*
 * amipr: TLS client over gfopentls.library.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 *
 * The library does the records and the handshake; the bytes go through
 * an I/O hook to the caller's transport (src/core/transport.c),
 * so no socket is handed to the library. The certificate is checked
 * (OTV_FULL: a chain to a CA in the trust store, its dates, the host
 * name), or it is the one certificate the caller pinned (its SHA-256):
 * how a self-signed RDP server the user chose to trust is accepted
 * (src/core/knownhosts.c). A handshake that fails on the certificate
 * leaves it readable (amipr_tls_peer_certificate, _fingerprint), so the
 * caller can show it and ask; it never goes on unverified (review H1).
 */
#include "amipr_internal.h"

#include <string.h>
#include <utility/hooks.h>
#include <clib/alib_protos.h> /* HookEntry */
#include <libraries/gfopentls.h>
#include <proto/gfopentls.h>

extern struct Library* OpenTLSBase;
struct Library* amipr_opentls_base(void);

struct amipr_tls
{
	struct Hook hook;
	struct OTContext* ctx;
	struct OTConnection* conn;
	amipr_tls_io io;
	void* user;
	uint8_t* cert;
	size_t cert_len;
	LONG last;
	int cert_error;   /* AMIPR_CERT_* of the last handshake */
	int verified;     /* 1: handshake done, certificate passed */
};

static ULONG io_hook(struct Hook* hook, struct OTConnection* conn, struct OTIOMessage* msg)
{
	struct amipr_tls* t = hook->h_Data;
	int n;
	(void)conn;
	n = t->io(t->user, msg->MethodID == OTIO_WRITE, msg->Buffer, (int)msg->Length);
	if (n == AMIPR_EAGAIN)
		return (ULONG)OTERR_WOULDBLOCK;
	if (n < 0)
		return (ULONG)OTERR_IO;
	/* recv 0 is end of stream: pass OTERR_CLOSED, not "0 bytes", so a close
	 * mid-record cannot spin as a short read (review L6) */
	if (n == 0 && msg->MethodID == OTIO_READ)
		return (ULONG)OTERR_CLOSED;
	return (ULONG)n;
}

const char* amipr_tls_backend_name(void)
{
	return "gfopentls.library";
}

struct amipr_tls* amipr_tls_new(const char* sni_host, amipr_tls_io io, void* user, const uint8_t* pin_sha256)
{
	struct OTContextConfig cfg;
	struct amipr_tls* t;
	LONG err = 0;

	if (!io || !amipr_opentls_base())
		return NULL;
	t = AllocVec(sizeof(*t), MEMF_ANY | MEMF_CLEAR);
	if (!t)
		return NULL;
	t->io = io;
	t->user = user;
	t->hook.h_Entry = (HOOKFUNC)(void (*)(void))HookEntry;
	t->hook.h_SubEntry = (HOOKFUNC)(void (*)(void))io_hook;
	t->hook.h_Data = t;

	memset(&cfg, 0, sizeof(cfg));
	cfg.Size = sizeof(cfg);
	/* the system trust store (S:GFOpenTLS/ca-bundle.pem) for servers with a
	 * CA's certificate; a missing store is OTERR_TRUSTSTORE at the
	 * handshake, a certificate error the caller can still ask about */
	cfg.Flags = 0;
	cfg.MinVersion = OT_TLS12;
	t->ctx = OT_NewContext(&cfg, &err);
	if (t->ctx)
		t->conn = OT_NewConnection(t->ctx, (CONST_STRPTR)(sni_host ? sni_host : ""), &err);
	if (!t->conn || OT_SetIOHook(t->conn, &t->hook) != OTERR_OK ||
	    OT_SetVerify(t->conn, OTV_FULL) != OTERR_OK ||
	    (pin_sha256 && OT_PinCertificate(t->conn, (CONST UBYTE*)pin_sha256) != OTERR_OK))
	{
		amipr_tls_free(t);
		return NULL;
	}
	return t;
}

void amipr_tls_free(struct amipr_tls* t)
{
	if (!t)
		return;
	if (t->conn)
		OT_FreeConnection(t->conn);
	if (t->ctx)
		OT_FreeContext(t->ctx);
	FreeVec(t->cert);
	FreeVec(t);
}

static int map(struct amipr_tls* t, LONG r)
{
	t->last = r;
	if (r >= 0)
		return (int)r;
	if (r == OTERR_WOULDBLOCK)
		return AMIPR_EAGAIN;
	if (r == OTERR_CLOSED)
		return AMIPR_ECLOSED;
	return AMIPR_ETLS;
}

int amipr_tls_handshake(struct amipr_tls* t)
{
	LONG r;
	if (!t)
		return AMIPR_EINVAL;
	r = OT_Handshake(t->conn);
	t->cert_error = 0;
	switch (r)
	{
		case OTERR_OK: t->verified = 1; break;
		case OTERR_UNTRUSTED: t->cert_error = AMIPR_CERT_UNTRUSTED; break;
		case OTERR_HOSTNAME: t->cert_error = AMIPR_CERT_HOSTNAME; break;
		case OTERR_EXPIRED: t->cert_error = AMIPR_CERT_EXPIRED; break;
		case OTERR_BADCERT: t->cert_error = AMIPR_CERT_BAD; break;
		case OTERR_TRUSTSTORE: t->cert_error = AMIPR_CERT_NOSTORE; break;
		case OTERR_PINNED: t->cert_error = AMIPR_CERT_PINNED; break;
		default: break;
	}
	if (t->cert_error)
	{
		t->last = r;
		return AMIPR_ECERT;
	}
	return map(t, r);
}

int amipr_tls_cert_error(struct amipr_tls* t)
{
	return t ? t->cert_error : 0;
}

int amipr_tls_verified(struct amipr_tls* t)
{
	return t ? t->verified : 0;
}

int amipr_tls_peer_fingerprint(struct amipr_tls* t, uint8_t out[32])
{
	if (!t || !out)
		return AMIPR_EINVAL;
	return OT_GetPeerFingerprint(t->conn, (UBYTE*)out) == OTERR_OK ? AMIPR_OK : AMIPR_ETLS;
}

int amipr_tls_read(struct amipr_tls* t, void* buf, int len)
{
	if (!t || !buf || len < 0)
		return AMIPR_EINVAL;
	return map(t, OT_Read(t->conn, buf, len));
}

int amipr_tls_write(struct amipr_tls* t, const void* buf, int len)
{
	if (!t || !buf || len < 0)
		return AMIPR_EINVAL;
	return map(t, OT_Write(t->conn, (CONST_APTR)buf, len));
}

int amipr_tls_pending(struct amipr_tls* t)
{
	LONG n = t ? OT_Pending(t->conn) : 0;
	return n > 0 ? (int)n : 0;
}

int amipr_tls_close(struct amipr_tls* t)
{
	if (!t)
		return AMIPR_EINVAL;
	return map(t, OT_Close(t->conn));
}

int amipr_tls_peer_certificate(struct amipr_tls* t, const uint8_t** der, size_t* len)
{
	LONG n;
	if (!t || !der || !len)
		return AMIPR_EINVAL;
	if (!t->cert)
	{
		n = OT_GetPeerCertificate(t->conn, NULL, 0);
		if (n <= 0)
			return AMIPR_ETLS;
		t->cert = AllocVec((ULONG)n, MEMF_ANY);
		if (!t->cert)
			return AMIPR_ENOMEM;
		n = OT_GetPeerCertificate(t->conn, t->cert, n);
		if (n <= 0)
		{
			FreeVec(t->cert);
			t->cert = NULL;
			return AMIPR_ETLS;
		}
		t->cert_len = (size_t)n;
	}
	*der = t->cert;
	*len = t->cert_len;
	return AMIPR_OK;
}

const char* amipr_tls_error_text(struct amipr_tls* t)
{
	if (!t)
		return "no TLS connection";
	if (t->conn)
		return (const char*)OT_GetErrorText(t->conn);
	return (const char*)OT_ErrorString(t->last);
}

const char* amipr_tls_cipher(struct amipr_tls* t)
{
	return t ? (const char*)OT_GetCipher(t->conn) : "";
}

int amipr_tls_protocol(struct amipr_tls* t)
{
	return t ? (int)OT_GetProtocol(t->conn) : 0;
}
