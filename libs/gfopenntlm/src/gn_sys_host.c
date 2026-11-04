/* gfopenntlm on a PC (the host tests): malloc, /dev/urandom, time().
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gn_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void *gn_alloc(size_t n) { return calloc(1, n ? n : 1); }
void gn_free(void *p, size_t n) { if (p) { gn_wipe(p, n); free(p); } }

int gn_random(void *buf, size_t len)
{
	FILE *f = fopen("/dev/urandom", "rb");
	size_t got;
	if (!f) return -1;
	got = fread(buf, 1, len, f);
	fclose(f);
	return got == len ? 0 : -1;
}

uint64_t gn_filetime_now(void)
{
	return ((uint64_t)time(NULL) + 11644473600ull) * 10000000ull;
}

void gn_sys_cleanup(void) { }
