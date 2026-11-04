/*
 * GFAmigaRDP: NLAStages, a check of the NTLMv2 key derivation one stage at
 * a time without showing any secret: for PASSWORD as ReadArgs gave it, it
 * prints only a 16-bit tag (the low half of a CRC-32) of the password bytes,
 * their UTF-16LE form, the NT hash (MD4), NTOWFv2 (HMAC-MD5) and an
 * NTProofStr over a fixed, non-secret input, each through gfopenntlm's
 * GN_Digest/GN_HMAC (the shared library, or libgfopenntlm.a when built with
 * GFOPENNTLM_STATIC). A PC computes the same tags to find the first stage
 * that differs. Never prints or writes the password or a hash of it in full.
 *
 *   NLAStages PASSWORD/A USER/K DOMAIN/K
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/gfopenntlm.h>
#include <stdio.h>
#include <string.h>
#include "nla_stages_data.h"

static const char version[] = "$VER: NLAStages 0.1 (04.11.2026) AmiCradle Team";

#define TEMPLATE "PASSWORD/A,USER/K,DOMAIN/K"
enum { A_PASSWORD, A_USER, A_DOMAIN, A_N };

#ifndef GFOPENNTLM_STATIC
struct Library* GFOpenNTLMBase;
#endif

static unsigned tag(const UBYTE* p, ULONG n)
{
	ULONG c = 0xffffffffUL;
	int k;
	while (n--)
	{
		c ^= *p++;
		for (k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xedb88320UL & (0UL - (c & 1)));
	}
	return (unsigned)((~c) & 0xffff);
}

/* ASCII/Latin-1 to UTF-16LE, upper-casing a-z when up (enough for a user name) */
static ULONG u16(const char* s, int up, UBYTE* out)
{
	ULONG n = 0;
	for (; *s; s++)
	{
		UBYTE c = (UBYTE)*s;
		if (up && c >= 'a' && c <= 'z') c -= 32;
		out[n++] = c;
		out[n++] = 0;
	}
	return n;
}

int main(void)
{
	LONG args[A_N] = { 0 };
	struct RDArgs* rd;
	static UBYTE pw16[512], ud[512], nt[16], owf[16], proof[16];
	const char *pw, *user, *dom;
	ULONG pl, ul;
	LONG e1, e2, e3;
	int rc = RETURN_FAIL;

	(void)version;
	rd = ReadArgs((CONST_STRPTR)TEMPLATE, args, NULL);
	if (!rd)
	{
		PrintFault(IoErr(), (CONST_STRPTR)"NLAStages");
		return RETURN_FAIL;
	}
	pw = (const char*)args[A_PASSWORD];
	user = args[A_USER] ? (const char*)args[A_USER] : "da1ek";
	dom = args[A_DOMAIN] ? (const char*)args[A_DOMAIN] : "";
	if (strlen(pw) > 200 || strlen(user) + strlen(dom) > 200)
	{
		printf("NLAStages: too long\n");
		goto out;
	}
#ifndef GFOPENNTLM_STATIC
	GFOpenNTLMBase = OpenLibrary((CONST_STRPTR)GFOPENNTLMLIB_NAME, GFOPENNTLMLIB_VERSION);
	if (!GFOpenNTLMBase)
	{
		printf("NLAStages: no %s\n", GFOPENNTLMLIB_NAME);
		goto out;
	}
	printf("shared ");
#else
	printf("static ");
#endif
	pl = u16(pw, 0, pw16);
	ul = u16(user, 1, ud);
	ul += u16(dom, 0, ud + ul);
	e1 = GN_Digest(GNALG_MD4, pw16, pl, nt);
	e2 = GN_HMAC(GNALG_MD5, nt, 16, ud, ul, owf);
	e3 = GN_HMAC(GNALG_MD5, owf, 16, proofdata, sizeof(proofdata), proof);
	printf("len %lu pw %04x u16 %04x nt %04x owf %04x proof %04x (%ld %ld %ld)\n",
	       (unsigned long)strlen(pw), tag((const UBYTE*)pw, strlen(pw)), tag(pw16, pl),
	       tag(nt, 16), tag(owf, 16), tag(proof, 16), (long)e1, (long)e2, (long)e3);
	memset(pw16, 0, sizeof(pw16));
	memset(nt, 0, 16);
	memset(owf, 0, 16);
	rc = RETURN_OK;
#ifndef GFOPENNTLM_STATIC
	CloseLibrary(GFOpenNTLMBase);
#endif
out:
	FreeArgs(rd);
	return rc;
}
