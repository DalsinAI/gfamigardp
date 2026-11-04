/* GFOpenNTLMLibTest: opens gfopenntlm.library and checks its calls on an
 * Amiga against published vectors (RFC 1320, RFC 2202, RFC 6229 and
 * [MS-NLMP] 4.2.4's NTOWFv2 through a NEGOTIATE). Linked with
 * -DGFOPENNTLM_STATIC and libgfopenntlm.a it checks the static link.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <string.h>
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/gfopenntlm.h>

#ifndef GFOPENNTLM_STATIC
struct Library *GFOpenNTLMBase;
#endif

static int pass, fail;
static void check(const char *what, int ok)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (ok) pass++; else fail++;
}

int main(void)
{
	static const UBYTE md4abc[16] = { 0xa4, 0x48, 0x01, 0x7a, 0xaf, 0x21, 0xd8, 0x52,
	                                  0x5f, 0xc1, 0x0a, 0xe8, 0x7a, 0xa6, 0x72, 0x9d };
	static const UBYTE hmac2[16] = { 0x75, 0x0c, 0x78, 0x3e, 0x6a, 0xb0, 0xb5, 0x03,
	                                 0xea, 0xa8, 0x6e, 0x31, 0x0a, 0x5d, 0xb7, 0x38 };
	static const UBYTE rc4[8] = { 0xb2, 0x39, 0x63, 0x05, 0xf0, 0x3d, 0xc0, 0x27 };
	static const UBYTE sha[4] = { 0xba, 0x78, 0x16, 0xbf };
	UBYTE d[64], key[5] = { 1, 2, 3, 4, 5 };
	struct GNNTLMConfig nc;
	struct GNNTLM *n;
	LONG e;
	ULONG l;
#ifndef GFOPENNTLM_STATIC
	GFOpenNTLMBase = OpenLibrary((CONST_STRPTR)GFOPENNTLMLIB_NAME, GFOPENNTLMLIB_VERSION);
	if (!GFOpenNTLMBase) { printf("FAIL cannot open %s %d\n", GFOPENNTLMLIB_NAME, GFOPENNTLMLIB_VERSION); return 20; }
	printf("%s %u.%u\n", GFOPENNTLMLIB_NAME, (unsigned)GFOpenNTLMBase->lib_Version, (unsigned)GFOpenNTLMBase->lib_Revision);
#endif
	check("GN_Version", GN_Version() == ((GN_LIB_VERSION << 16) | GN_LIB_REVISION));
	check("GN_Digest MD4 \"abc\" (RFC 1320)", GN_Digest(GNALG_MD4, "abc", 3, d) == 0 && !memcmp(d, md4abc, 16));
	check("GN_Digest SHA-256 \"abc\" (FIPS 180-2)", GN_Digest(GNALG_SHA256, "abc", 3, d) == 0 && !memcmp(d, sha, 4));
	check("GN_HMAC MD5 #2 (RFC 2202)", GN_HMAC(GNALG_MD5, "Jefe", 4, "what do ya want for nothing?", 28, d) == 0 &&
	      !memcmp(d, hmac2, 16));
	memset(d, 0, 16);
	check("GN_RC4 40-bit (RFC 6229)", GN_RC4(key, 5, d, 16) == 0 && !memcmp(d, rc4, 8));
	memset(&nc, 0, sizeof nc);
	nc.Size = sizeof nc; nc.User = "User"; nc.Domain = "Domain"; nc.Password = "Password";
	n = GN_NTLMNew(&nc, &e);
	check("GN_NTLMNew", n != NULL && e == GNERR_OK);
	check("GN_NTLMNegotiate", n && GN_NTLMNegotiate(n, d, sizeof d, &l) == 0 && l == 40 && !memcmp(d, "NTLMSSP", 8));
	GN_NTLMFree(n);
	check("GN_ErrorText", !strcmp((const char *)GN_ErrorText(GNERR_SERVER), "the server reported an error"));
	printf("%d passed, %d failed\n", pass, fail);
#ifndef GFOPENNTLM_STATIC
	CloseLibrary(GFOpenNTLMBase);
#endif
	return fail ? 10 : 0;
}
