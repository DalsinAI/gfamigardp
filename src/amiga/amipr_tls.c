/*
 * amipr: TLS client over gfopentls.library.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 *
 * The library does the records and the handshake; the bytes go through
 * an I/O hook to the caller's transport (src/core/transport.c),
 * so no socket is handed to the library. The certificate is not checked
 * here (OTV_NONE): the caller reads the leaf with
 * amipr_tls_peer_certificate() and decides (known hosts, fingerprints,
 * asking the user), as RDP clients do for self-signed servers.
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
	return (ULONG)n;
}

const char* amipr_tls_backend_name(void)
{
	return "gfopentls.library";
}

struct amipr_tls* amipr_tls_new(const char* sni_host, amipr_tls_io io, void* user)
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
	/* the trust store is not consulted with OTV_NONE: skip reading it */
	cfg.Flags = OTCF_NO_SYSTEM_TRUST;
	cfg.MinVersion = OT_TLS12;
	t->ctx = OT_NewContext(&cfg, &err);
	if (t->ctx)
		t->conn = OT_NewConnection(t->ctx, (CONST_STRPTR)(sni_host ? sni_host : ""), &err);
	if (!t->conn || OT_SetIOHook(t->conn, &t->hook) != OTERR_OK ||
	    OT_SetVerify(t->conn, OTV_NONE) != OTERR_OK)
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
	if (!t)
		return AMIPR_EINVAL;
	return map(t, OT_Handshake(t->conn));
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
