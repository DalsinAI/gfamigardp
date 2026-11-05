/*
 * GFAmigaRDP: Network Level Authentication ([MS-RDPBCGR] 5.4.5.2) with
 * CredSSP ([MS-CSSP]) from gfopenntlm.library. Each TSRequest goes over
 * TLS in one write; each reply is read as one DER message.
 * NTSTATUS values are from [MS-ERREF] 2.3.1, SEC_E_ values from 2.1.1.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "nla.h"
#include "amipr.h"

#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/gfopenntlm.h>
#include <string.h>

#ifndef GFOPENNTLM_STATIC
struct Library* GFOpenNTLMBase;
#endif

static const struct
{
	uint32_t code;
	const char* name;
	const char* text;
} statuses[] = {
	{ 0xC000006Du, "STATUS_LOGON_FAILURE", "The user name or password is not right." },
	{ 0xC000006Au, "STATUS_WRONG_PASSWORD", "The password is not right." },
	{ 0xC0000064u, "STATUS_NO_SUCH_USER", "There is no such user." },
	{ 0xC000006Eu, "STATUS_ACCOUNT_RESTRICTION", "The account may not log on (a blank password, or a restriction)." },
	{ 0xC000006Fu, "STATUS_INVALID_LOGON_HOURS", "The account may not log on at this time." },
	{ 0xC0000070u, "STATUS_INVALID_WORKSTATION", "The account may not log on from this computer." },
	{ 0xC0000071u, "STATUS_PASSWORD_EXPIRED", "The password has expired." },
	{ 0xC0000072u, "STATUS_ACCOUNT_DISABLED", "The account is disabled." },
	{ 0xC0000193u, "STATUS_ACCOUNT_EXPIRED", "The account has expired." },
	{ 0xC0000224u, "STATUS_PASSWORD_MUST_CHANGE", "The password must be changed first." },
	{ 0xC0000234u, "STATUS_ACCOUNT_LOCKED_OUT", "The account is locked." },
	{ 0xC000015Bu, "STATUS_LOGON_TYPE_NOT_GRANTED", "The account may not log on remotely." },
	{ 0xC0000022u, "STATUS_ACCESS_DENIED", "Access was denied." },
	{ 0xC00000BBu, "STATUS_NOT_SUPPORTED", "The server does not support this CredSSP version." },
	{ 0xC000000Du, "STATUS_INVALID_PARAMETER", "The server found a message not valid." },
	{ 0xC0000388u, "STATUS_DOWNGRADE_DETECTED", "The server saw a downgrade." },
	{ 0x8009030Cu, "SEC_E_LOGON_DENIED", "The logon was denied." },
	{ 0x80090308u, "SEC_E_INVALID_TOKEN", "The server found an NTLM message not valid." },
	{ 0x80090302u, "SEC_E_UNSUPPORTED_FUNCTION", "The server does not support this." },
	{ 0x8009030Eu, "SEC_E_NO_CREDENTIALS", "No credentials were given." },
	{ 0x80090311u, "SEC_E_NO_AUTHENTICATING_AUTHORITY", "The server could not reach its domain controller." },
	{ 0x80090322u, "SEC_E_WRONG_PRINCIPAL", "The server's name is not the one asked for." },
	{ 0x80090327u, "SEC_E_CERT_UNKNOWN", "The certificate is unknown." },
	{ 0x8009030Fu, "SEC_E_MESSAGE_ALTERED", "A message was changed on its way." },
};

static int find_status(uint32_t code)
{
	unsigned i;
	for (i = 0; i < sizeof(statuses) / sizeof(statuses[0]); i++)
		if (statuses[i].code == code)
			return (int)i;
	return -1;
}

const char* rdp_ntstatus_name(uint32_t code)
{
	int i = find_status(code);
	return i < 0 ? NULL : statuses[i].name;
}

const char* rdp_ntstatus_text(uint32_t code)
{
	int i = find_status(code);
	return i < 0 ? NULL : statuses[i].text;
}

/* s->nla_dump: one TSRequest as a line of hex, "C>S 1 <hex>" or "S>C 1 <hex>" */
static void dump_line(BPTR f, const char* dir, int n, const uint8_t* p, size_t len)
{
	static const char hx[] = "0123456789abcdef";
	char line[80];
	size_t i, k = 0;
	if (!f)
		return;
	line[k++] = dir[0]; line[k++] = dir[1]; line[k++] = dir[2]; line[k++] = ' ';
	line[k++] = (char)('0' + n); line[k++] = ' ';
	for (i = 0; i < len; i++)
	{
		line[k++] = hx[p[i] >> 4];
		line[k++] = hx[p[i] & 15];
		if (k >= sizeof(line) - 2)
		{
			Write(f, line, (LONG)k);
			k = 0;
		}
	}
	line[k++] = '\n';
	Write(f, line, (LONG)k);
}

/* one TSRequest: a DER SEQUENCE, read as its length says */
static int read_tsrequest(struct rdp_transport* t, uint8_t* buf, size_t size)
{
	int rc;
	LONG need;
	size_t have = 2;

	rc = rdp_transport_read_full(t, buf, 2);
	if (rc < 0)
		return rc;
	if (buf[0] != 0x30)
		return RDP_E_PROTO;
	if (buf[1] & 0x80)
	{
		size_t k = buf[1] & 0x7f;
		if (k < 1 || k > 3)
			return RDP_E_PROTO;
		rc = rdp_transport_read_full(t, buf + 2, k);
		if (rc < 0)
			return rc;
		have += k;
	}
	need = GN_CredSSPMessageLength(buf, (ULONG)have);
	if (need <= 0)
		return RDP_E_PROTO;
	if ((size_t)need > size)
		return RDP_E_SPACE;
	rc = rdp_transport_read_full(t, buf + have, (size_t)need - have);
	return rc < 0 ? rc : (int)need;
}

int rdp_nla(struct rdp_transport* t, const struct rdp_settings* s, const char* password,
            const uint8_t* pubkey, size_t pubkey_len, const uint8_t* cb, size_t cb_len,
            int hybrid_ex, struct rdp_nla_result* res)
{
	struct GNCredSSPConfig cfg;
	struct GNCredSSP* c;
	uint8_t* in;
	uint8_t* out;
	ULONG outlen = 0;
	LONG r, err = 0;
	int rc = 0, inlen = 0;
	BPTR dump = 0;

	memset(res, 0, sizeof(*res));
#ifndef GFOPENNTLM_STATIC
	GFOpenNTLMBase = OpenLibrary((CONST_STRPTR)GFOPENNTLMLIB_NAME, GFOPENNTLMLIB_VERSION);
	if (!GFOpenNTLMBase)
		return AMIPR_ENOLIB;
#endif
	in = AllocVec(2 * GN_CREDSSP_BUFFER, MEMF_ANY);
	out = in ? in + GN_CREDSSP_BUFFER : NULL;
	memset(&cfg, 0, sizeof(cfg));
	cfg.Size = sizeof(cfg);
	cfg.User = s->user;
	cfg.Domain = s->domain;
	cfg.Password = password;
	cfg.Workstation = "AMIGA";
	cfg.ServerName = s->host;
	cfg.PublicKey = pubkey;
	cfg.PublicKeyLength = (ULONG)pubkey_len;
	/* gfopenntlm 1.1: the TLS channel bindings, the certificate hash; the
	 * library adds "tls-server-end-point:" (RFC 5929 4.1). A 1.0 library
	 * reads only up to MaxVersion and leaves the exchange unbound. */
	if (cb && cb_len)
	{
		cfg.Flags = GNF_CB_TLS_SERVER_END_POINT;
		cfg.ChannelBindings = cb;
		cfg.ChannelBindingsLength = (ULONG)cb_len;
	}
	c = in ? GN_CredSSPNew(&cfg, &err) : NULL;
	if (!c)
	{
		res->gnerr = in ? err : GNERR_NOMEM;
		rc = RDP_E_NLA;
		goto done;
	}
	if (s->nla_dump)
		dump = Open((CONST_STRPTR)s->nla_dump, MODE_NEWFILE);
	for (;;)
	{
		r = GN_CredSSPStep(c, res->stage ? in : NULL, (ULONG)inlen, out, GN_CREDSSP_BUFFER, &outlen);
		if (r < 0)
		{
			res->gnerr = r;
			res->server_error = GN_CredSSPGetAttr(c, GNA_SERVERERROR);
			rc = RDP_E_NLA;
			break;
		}
		if (res->stage < 2)  /* not the TSCredentials */
			dump_line(dump, "C>S", res->stage + 1, out, outlen);
		rc = rdp_transport_write(t, out, outlen);
		memset(out, 0, outlen);  /* the last one holds the sealed password */
		if (rc < 0)
			break;
		res->stage++;
		if (r == GNERR_OK)
			break;
		inlen = read_tsrequest(t, in, GN_CREDSSP_BUFFER);
		if (inlen < 0)
		{
			rc = inlen;
			if (rc == AMIPR_ECLOSED)
				res->closed_after = res->stage;
			break;
		}
		if (res->stage <= 2)
			dump_line(dump, "S>C", res->stage, in, (size_t)inlen);
	}
	if (dump)
		Close(dump);
	res->version = GN_CredSSPGetAttr(c, GNA_VERSION);
	res->peer_version = GN_CredSSPGetAttr(c, GNA_PEERVERSION);
	res->ntlm_peer_version = GN_CredSSPGetAttr(c, GNA_NTLMPEERVERSION);
	res->bound = GN_CredSSPGetAttr(c, GNA_CHANNELBINDINGS) != 0;
	GN_CredSSPFree(c);
	/* HYBRID_EX: the server answers the credentials with a 4-byte Early
	 * User Authorization Result ([MS-RDPBCGR] 2.2.10.2) */
	if (rc == 0 && hybrid_ex)
	{
		uint8_t e[4];
		rc = rdp_transport_read_full(t, e, 4);
		if (rc == 0)
		{
			res->early_auth = (uint32_t)e[0] | ((uint32_t)e[1] << 8) | ((uint32_t)e[2] << 16) | ((uint32_t)e[3] << 24);
			if (res->early_auth != 0)
				rc = RDP_E_NLA;
		}
		else if (rc == AMIPR_ECLOSED)
			res->closed_after = res->stage;
	}
done:
	res->lib_error = (const char*)GN_ErrorText(res->gnerr);
	if (in)
	{
		memset(in, 0, 2 * GN_CREDSSP_BUFFER);
		FreeVec(in);
	}
#ifndef GFOPENNTLM_STATIC
	CloseLibrary(GFOpenNTLMBase);
	GFOpenNTLMBase = NULL;
#endif
	return rc;
}
