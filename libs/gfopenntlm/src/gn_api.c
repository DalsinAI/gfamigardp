/* gfopenntlm: the public calls (include/clib/gfopenntlm_protos.h), the
 * same C functions for the library (library/gn_lib.c's entry points) and
 * the static link. MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gn_internal.h"
#include <clib/gfopenntlm_protos.h>
#include <stddef.h>
#include <string.h>

struct GNNTLM { struct gn_ntlm n; };
struct GNCredSSP { struct gn_cssp c; };

#define HAS(cfg, field) ((cfg)->Size >= offsetof(__typeof__(*(cfg)), field) + sizeof((cfg)->field))

ULONG GN_Version(VOID)
{
	return ((ULONG)GN_LIB_VERSION << 16) | GN_LIB_REVISION;
}

CONST_STRPTR GN_ErrorText(LONG error)
{
	switch (error) {
	case GNERR_OK: return (CONST_STRPTR)"no error";
	case GNERR_CONTINUE: return (CONST_STRPTR)"send the output and continue";
	case GNERR_NOMEM: return (CONST_STRPTR)"out of memory";
	case GNERR_BADARGS: return (CONST_STRPTR)"bad arguments";
	case GNERR_BUFFER: return (CONST_STRPTR)"output buffer too small";
	case GNERR_PROTOCOL: return (CONST_STRPTR)"malformed message from the peer";
	case GNERR_SERVER: return (CONST_STRPTR)"the server reported an error";
	case GNERR_PUBKEY: return (CONST_STRPTR)"the server's public key binding does not match";
	case GNERR_INTEGRITY: return (CONST_STRPTR)"message signature does not verify";
	case GNERR_NORANDOM: return (CONST_STRPTR)"no random source (gfopentls.library 1 needed)";
	case GNERR_STATE: return (CONST_STRPTR)"call out of order";
	case GNERR_UNSUPPORTED: return (CONST_STRPTR)"the server asks for an unsupported feature";
	default: return (CONST_STRPTR)"unknown error";
	}
}

static void seterr(LONG *error, LONG e) { if (error) *error = e; }

struct GNNTLM *GN_NTLMNew(CONST struct GNNTLMConfig *config, LONG *error)
{
	struct GNNTLM *h;
	int e;
	if (!config || !HAS(config, TargetName)) { seterr(error, GNERR_BADARGS); return NULL; }
	h = gn_alloc(sizeof *h);
	if (!h) { seterr(error, GNERR_NOMEM); return NULL; }
	e = gn_ntlm_init(&h->n, config->User, config->Domain,
	                 config->Password, config->Workstation,
	                 config->TargetName);
	if (e) { gn_free(h, sizeof *h); seterr(error, e); return NULL; }
	seterr(error, GNERR_OK);
	return h;
}

VOID GN_NTLMFree(struct GNNTLM *ntlm)
{
	if (!ntlm) return;
	gn_ntlm_clear(&ntlm->n);
	gn_free(ntlm, sizeof *ntlm);
}

LONG GN_NTLMNegotiate(struct GNNTLM *ntlm, APTR out, ULONG size, ULONG *length)
{
	size_t n = 0;
	int e;
	if (!ntlm || !out || !length) return GNERR_BADARGS;
	e = gn_ntlm_negotiate(&ntlm->n, out, size, &n);
	*length = (ULONG)n;
	return e;
}

LONG GN_NTLMAuthenticate(struct GNNTLM *ntlm, CONST_APTR challenge, ULONG challengeLength,
                         APTR out, ULONG size, ULONG *length)
{
	size_t n = 0;
	int e;
	if (!ntlm || !challenge || !out || !length) return GNERR_BADARGS;
	e = gn_ntlm_authenticate(&ntlm->n, challenge, challengeLength, out, size, &n);
	*length = (ULONG)n;
	return e;
}

LONG GN_NTLMWrap(struct GNNTLM *ntlm, CONST_APTR in, ULONG inLength, APTR out, ULONG size, ULONG *length)
{
	size_t n = 0;
	int e;
	if (!ntlm || (!in && inLength) || !out || !length) return GNERR_BADARGS;
	e = gn_ntlm_wrap(&ntlm->n, in, inLength, out, size, &n);
	*length = (ULONG)n;
	return e;
}

LONG GN_NTLMUnwrap(struct GNNTLM *ntlm, CONST_APTR in, ULONG inLength, APTR out, ULONG size, ULONG *length)
{
	size_t n = 0;
	int e;
	if (!ntlm || !in || !out || !length) return GNERR_BADARGS;
	e = gn_ntlm_unwrap(&ntlm->n, in, inLength, out, size, &n);
	*length = (ULONG)n;
	return e;
}

ULONG GN_NTLMGetAttr(struct GNNTLM *ntlm, ULONG attr)
{
	if (!ntlm) return 0;
	switch (attr) {
	case GNA_NTLMFLAGS: return ntlm->n.flags;
	case GNA_NTLMPEERVERSION: return ntlm->n.peer_version;
	case GNA_STATE: return (ULONG)ntlm->n.state;
	default: return 0;
	}
}

struct GNCredSSP *GN_CredSSPNew(CONST struct GNCredSSPConfig *config, LONG *error)
{
	struct GNCredSSP *h;
	int e;
	if (!config || !HAS(config, MaxVersion) || !config->PublicKey || !config->PublicKeyLength) {
		seterr(error, GNERR_BADARGS);
		return NULL;
	}
	h = gn_alloc(sizeof *h);
	if (!h) { seterr(error, GNERR_NOMEM); return NULL; }
	e = gn_cssp_init(&h->c, config->User, config->Domain,
	                 config->Password, config->Workstation,
	                 config->ServerName, config->PublicKey,
	                 config->PublicKeyLength, config->MaxVersion);
	if (e) { gn_free(h, sizeof *h); seterr(error, e); return NULL; }
	seterr(error, GNERR_OK);
	return h;
}

VOID GN_CredSSPFree(struct GNCredSSP *credssp)
{
	if (!credssp) return;
	gn_cssp_clear(&credssp->c);
	gn_free(credssp, sizeof *credssp);
}

LONG GN_CredSSPStep(struct GNCredSSP *credssp, CONST_APTR in, ULONG inLength,
                    APTR out, ULONG size, ULONG *length)
{
	size_t n = 0;
	int e;
	if (!credssp || !out || !length || (credssp->c.state && !in)) return GNERR_BADARGS;
	e = gn_cssp_step(&credssp->c, in, inLength, out, size, &n);
	*length = (ULONG)n;
	return e;
}

ULONG GN_CredSSPGetAttr(struct GNCredSSP *credssp, ULONG attr)
{
	if (!credssp) return 0;
	switch (attr) {
	case GNA_VERSION: return credssp->c.version;
	case GNA_PEERVERSION: return credssp->c.peer_version;
	case GNA_SERVERERROR: return credssp->c.server_error;
	case GNA_STATE: return (ULONG)credssp->c.state;
	case GNA_LASTERROR: return (ULONG)credssp->c.error;
	default: return GN_NTLMGetAttr((struct GNNTLM *)(void *)&credssp->c.ntlm, attr);
	}
}

LONG GN_Digest(ULONG algorithm, CONST_APTR data, ULONG length, APTR out)
{
	if ((!data && length) || !out) return GNERR_BADARGS;
	switch (algorithm) {
	case GNALG_MD4: gn_md4(data, length, out); return GNERR_OK;
	case GNALG_MD5: gn_md5(data, length, out); return GNERR_OK;
	case GNALG_SHA256: gn_sha256(data, length, out); return GNERR_OK;
	default: return GNERR_BADARGS;
	}
}

LONG GN_HMAC(ULONG algorithm, CONST_APTR key, ULONG keyLength, CONST_APTR data, ULONG length, APTR out)
{
	if ((!key && keyLength) || (!data && length) || !out) return GNERR_BADARGS;
	switch (algorithm) {
	case GNALG_MD5: gn_hmac_md5(key, keyLength, data, length, out); return GNERR_OK;
	case GNALG_SHA256: gn_hmac_sha256(key, keyLength, data, length, out); return GNERR_OK;
	default: return GNERR_BADARGS;
	}
}

LONG GN_RC4(CONST_APTR key, ULONG keyLength, APTR data, ULONG length)
{
	struct gn_rc4 r;
	if (!key || !keyLength || (!data && length)) return GNERR_BADARGS;
	gn_rc4_init(&r, key, keyLength);
	gn_rc4_crypt(&r, data, data, length);
	gn_wipe(&r, sizeof r);
	return GNERR_OK;
}

LONG GN_CredSSPMessageLength(CONST_APTR data, ULONG length)
{
	long n;
	if (!data) return GNERR_BADARGS;
	n = gn_der_message_length(data, length);
	if (n < 0 || ((const UBYTE *)data)[0] != 0x30) return GNERR_PROTOCOL;
	return (LONG)n;
}
