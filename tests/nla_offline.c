/*
 * GFAmigaRDP: NLAOffline, gfopenntlm.library's CredSSP client on a
 * captured CHALLENGE, with no network: reads the "S>C 1" line of an
 * RDPProbe DUMP file, runs GN_CredSSPNew/GN_CredSSPStep on it and writes
 * a dump of the same form (the NEGOTIATE, that CHALLENGE and the
 * AUTHENTICATE), for gfopenntlm's tools/nla_replay.py to check on a host.
 * With a test password this checks the library on real server data; with
 * the real one, that the password reached the library unchanged. The
 * password is never printed or written.
 *
 *   NLAOffline FROM/A TO/A USER/K DOMAIN/K PASSWORD/K SERVER/K
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include <dos/dos.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/gfopenntlm.h>
#include <stdio.h>
#include <string.h>

static const char version[] = "$VER: NLAOffline 0.1 (04.11.2026) AmiCradle Team";

#define TEMPLATE "FROM/A,TO/A,USER/K,DOMAIN/K,PASSWORD/K,SERVER/K"
enum { A_FROM, A_TO, A_USER, A_DOMAIN, A_PASSWORD, A_SERVER, A_N };

#ifndef GFOPENNTLM_STATIC
struct Library* GFOpenNTLMBase;
#endif

static int hexval(int c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static void put_line(BPTR f, const char* tag, const UBYTE* p, ULONG len)
{
	static const char hx[] = "0123456789abcdef";
	char buf[64];
	ULONG i, k = 0;
	Write(f, (APTR)tag, (LONG)strlen(tag));
	for (i = 0; i < len; i++)
	{
		buf[k++] = hx[p[i] >> 4];
		buf[k++] = hx[p[i] & 15];
		if (k >= sizeof(buf))
		{
			Write(f, buf, (LONG)k);
			k = 0;
		}
	}
	buf[k++] = '\n';
	Write(f, buf, (LONG)k);
}

int main(void)
{
	LONG args[A_N] = { 0 };
	struct RDArgs* rd;
	static UBYTE text[16384], chal[8192], out[8192], pub[270];
	struct GNCredSSPConfig cfg;
	struct GNCredSSP* c;
	ULONG cl = 0, outlen = 0;
	LONG n, e = 0, r;
	BPTR f;
	char* p;
	int rc = RETURN_FAIL;

	(void)version;
	rd = ReadArgs((CONST_STRPTR)TEMPLATE, args, NULL);
	if (!rd)
	{
		PrintFault(IoErr(), (CONST_STRPTR)"NLAOffline");
		return RETURN_FAIL;
	}
	f = Open((CONST_STRPTR)args[A_FROM], MODE_OLDFILE);
	n = f ? Read(f, text, sizeof(text) - 1) : -1;
	if (f) Close(f);
	if (n <= 0)
	{
		printf("NLAOffline: cannot read %s\n", (char*)args[A_FROM]);
		goto out;
	}
	text[n] = 0;
	p = strstr((char*)text, "S>C 1 ");
	if (!p)
	{
		printf("NLAOffline: no \"S>C 1\" line in %s\n", (char*)args[A_FROM]);
		goto out;
	}
	for (p += 6; hexval(p[0]) >= 0 && hexval(p[1]) >= 0 && cl < sizeof(chal); p += 2)
		chal[cl++] = (UBYTE)(hexval(p[0]) << 4 | hexval(p[1]));
#ifndef GFOPENNTLM_STATIC
	GFOpenNTLMBase = OpenLibrary((CONST_STRPTR)GFOPENNTLMLIB_NAME, GFOPENNTLMLIB_VERSION);
	if (!GFOpenNTLMBase)
	{
		printf("NLAOffline: no %s %d\n", GFOPENNTLMLIB_NAME, GFOPENNTLMLIB_VERSION);
		goto out;
	}
	printf("%s %u.%u, ", GFOPENNTLMLIB_NAME, (unsigned)GFOpenNTLMBase->lib_Version, (unsigned)GFOpenNTLMBase->lib_Revision);
#endif
	printf("CHALLENGE TSRequest %lu bytes\n", (unsigned long)cl);
	memcpy(pub, "\x30\x82\x01\x0a\x02\x82\x01\x01\x00\xc3", 10);
	memset(&cfg, 0, sizeof(cfg));
	cfg.Size = sizeof(cfg);
	cfg.User = args[A_USER] ? (const char*)args[A_USER] : "da1ek";
	cfg.Domain = (const char*)args[A_DOMAIN];
	cfg.Password = args[A_PASSWORD] ? (const char*)args[A_PASSWORD] : "test123";
	cfg.Workstation = "AMIGA";
	cfg.ServerName = args[A_SERVER] ? (const char*)args[A_SERVER] : "192.168.0.195";
	cfg.PublicKey = pub;
	cfg.PublicKeyLength = sizeof(pub);
	c = GN_CredSSPNew(&cfg, &e);
	if (!c)
	{
		printf("NLAOffline: GN_CredSSPNew %ld\n", (long)e);
		goto close;
	}
	f = Open((CONST_STRPTR)args[A_TO], MODE_NEWFILE);
	if (!f)
	{
		printf("NLAOffline: cannot write %s\n", (char*)args[A_TO]);
		GN_CredSSPFree(c);
		goto close;
	}
	r = GN_CredSSPStep(c, NULL, 0, out, sizeof(out), &outlen);
	printf("step 1: %ld, %lu bytes\n", (long)r, (unsigned long)outlen);
	if (r >= 0)
	{
		put_line(f, "C>S 1 ", out, outlen);
		put_line(f, "S>C 1 ", chal, cl);
		r = GN_CredSSPStep(c, chal, cl, out, sizeof(out), &outlen);
		printf("step 2: %ld, %lu bytes\n", (long)r, (unsigned long)outlen);
		if (r >= 0)
		{
			put_line(f, "C>S 2 ", out, outlen);
			rc = RETURN_OK;
		}
	}
	Close(f);
	GN_CredSSPFree(c);
close:
#ifndef GFOPENNTLM_STATIC
	CloseLibrary(GFOpenNTLMBase);
#endif
out:
	FreeArgs(rd);
	return rc;
}
