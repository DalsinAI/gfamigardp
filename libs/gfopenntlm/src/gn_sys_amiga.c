/* gfopenntlm on AmigaOS 3.x: exec AllocVec, gfopentls.library's
 * OT_Random() for random bytes, dos DateStamp() for the clock.
 * Used by both the library and the static link (libgfopenntlm.a).
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gn_internal.h"
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <inline/macros.h>

/* A private base, so a program that links this statically and opens
 * gfopentls.library itself keeps its own OpenTLSBase. */
static struct Library *GN_TLSBase;

/* gfopentls.library 1: LONG OT_Random(APTR buffer a0, LONG length d0),
 * offset -204 (its autodoc); answers 0 on success. */
#define GN_OT_Random(buffer, length) \
	LP2(0xcc, LONG, GN_OT_Random, APTR, buffer, a0, LONG, length, d0, , GN_TLSBase)

void *gn_alloc(size_t n) { return AllocVec(n ? n : 1, MEMF_ANY | MEMF_CLEAR); }
void gn_free(void *p, size_t n) { if (p) { gn_wipe(p, n); FreeVec(p); } }

int gn_random(void *buf, size_t len)
{
	uint8_t *p = buf;
	if (!GN_TLSBase) {
		struct Library *b = OpenLibrary((CONST_STRPTR)"gfopentls.library", 1);
		if (!b) return -1;
		Forbid();
		if (!GN_TLSBase) { GN_TLSBase = b; b = NULL; }
		Permit();
		if (b) CloseLibrary(b);
	}
	while (len) {
		LONG n = len > 0x10000 ? 0x10000 : (LONG)len;
		if (GN_OT_Random(p, n) != 0) return -1;
		p += n; len -= (size_t)n;
	}
	return 0;
}

/* DateStamp counts from 1978-01-01 local time; AmigaOS 3 keeps no zone, so
 * this is the local clock taken as UTC. NTLM uses the server's
 * MsvAvTimestamp when the CHALLENGE has one (every Windows since Vista). */
uint64_t gn_filetime_now(void)
{
	struct DateStamp ds;
	uint64_t s;
	DateStamp(&ds);
	s = (uint64_t)ds.ds_Days * 86400u + (uint64_t)ds.ds_Minute * 60u + (uint64_t)(ds.ds_Tick / TICKS_PER_SECOND);
	return (s + 11896934400ull) * 10000000ull + (uint64_t)(ds.ds_Tick % TICKS_PER_SECOND) * 200000u;
}

void gn_sys_cleanup(void)
{
	if (GN_TLSBase) { CloseLibrary(GN_TLSBase); GN_TLSBase = NULL; }
}
