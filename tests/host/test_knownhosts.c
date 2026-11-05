/*
 * GFAmigaRDP host tests: the known hosts file (knownhosts.c).
 * cc -I../../src/core test_knownhosts.c ../../src/core/knownhosts.c
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "knownhosts.h"

static int fails, passes;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
	char path[] = "/tmp/gfamigardp-kh-XXXXXX";
	char file[64];
	uint8_t a[32], b[32], got[32];
	char text[96];
	int i;
	FILE* fh;

	if (!mkdtemp(path))
		return 1;
	snprintf(file, sizeof(file), "%s/sub/KnownHosts", path);   /* the drawer is made too */
	rdp_known_hosts_path = file;
	for (i = 0; i < 32; i++)
	{
		a[i] = (uint8_t)i;
		b[i] = (uint8_t)(0xFF - i);
	}
	CHECK(rdp_known_host_get("pc", 3389, got) == 0, "empty");
	CHECK(rdp_known_host_set("pc", 3389, a) == 0, "set");
	CHECK(rdp_known_host_get("pc", 3389, got) == 1 && !memcmp(got, a, 32), "get");
	CHECK(rdp_known_host_get("pc", 3390, got) == 0, "another port");
	CHECK(rdp_known_host_get("pc2", 3389, got) == 0, "another host");
	CHECK(rdp_known_host_set("other.example.org", 3389, b) == 0, "second");
	CHECK(rdp_known_host_set("pc", 3389, b) == 0, "replace");
	CHECK(rdp_known_host_get("pc", 3389, got) == 1 && !memcmp(got, b, 32), "replaced");
	CHECK(rdp_known_host_get("other.example.org", 3389, got) == 1 && !memcmp(got, b, 32), "kept");
	CHECK(rdp_known_host_set("bad host", 3389, a) < 0, "a space");
	CHECK(rdp_known_host_set("", 3389, a) < 0, "empty name");
	{
		int lines = 0;
		char line[400];
		fh = fopen(file, "r");
		while (fh && fgets(line, sizeof(line), fh))
			if (line[0] != '#')
				lines++;
		if (fh)
			fclose(fh);
		CHECK(lines == 2, "%d host lines, not 2", lines);
	}
	rdp_fingerprint_text(a, text);
	CHECK(strlen(text) == 95 && !strncmp(text, "00:01:02", 8) && !strcmp(text + 93, "1F"), "text %s", text);
	/* a broken line is skipped */
	fh = fopen(file, "a");
	fputs("junk 12:34\n", fh);
	fclose(fh);
	CHECK(rdp_known_host_get("junk", 0, got) == 0, "junk");

	/* review 2 N3: the host is compared in lower case, without a trailing dot */
	CHECK(rdp_known_host_get("PC", 3389, got) == 1 && !memcmp(got, b, 32), "upper case finds it");
	CHECK(rdp_known_host_get("Other.Example.ORG.", 3389, got) == 1, "case and trailing dot");
	CHECK(rdp_known_host_set("Other.EXAMPLE.org", 3389, a) == 0, "replace by another spelling");
	CHECK(rdp_known_host_get("other.example.org", 3389, got) == 1 && !memcmp(got, a, 32), "one pin for both spellings");
	{
		/* a line an older GFAmigaRDP wrote in mixed case is found and replaced too */
		fh = fopen(file, "a");
		rdp_fingerprint_text(a, text);
		fprintf(fh, "Mixed.Example.Org:3389 %s\n", text);
		fclose(fh);
		CHECK(rdp_known_host_get("mixed.example.org", 3389, got) == 1 && !memcmp(got, a, 32), "old mixed-case line");
		CHECK(rdp_known_host_set("MIXED.example.org", 3389, b) == 0, "replace it");
		CHECK(rdp_known_host_get("Mixed.Example.Org", 3389, got) == 1 && !memcmp(got, b, 32), "replaced");
	}

	/* no cap: 300 hosts and comments, and every one is still pinned */
	{
		char h[64], tmp[96];
		int ok = 1;
		fh = fopen(file, "a");
		for (i = 0; i < 40; i++)
			fprintf(fh, "# a comment %d\n", i);
		fclose(fh);
		for (i = 0; i < 300 && ok; i++)
		{
			snprintf(h, sizeof(h), "host%03d.example.org", i);
			ok = rdp_known_host_set(h, 3389, (i & 1) ? a : b) == 0;
		}
		CHECK(ok, "300 sets");
		for (i = 0; i < 300 && ok; i++)
		{
			snprintf(h, sizeof(h), "host%03d.example.org", i);
			ok = rdp_known_host_get(h, 3389, got) == 1 && !memcmp(got, (i & 1) ? a : b, 32);
		}
		CHECK(ok, "host%03d lost", i - 1);
		CHECK(rdp_known_host_get("pc", 3389, got) == 1 && !memcmp(got, b, 32), "the first host still there");

		/* written beside it and renamed over it: no .new left behind */
		snprintf(tmp, sizeof(tmp), "%s.new", file);
		CHECK(access(tmp, F_OK) != 0, "%s left behind", tmp);
		/* a crash after the old file went and before the rename: the
		 * new one is read, and the next write puts the file back */
		CHECK(rename(file, tmp) == 0, "simulate the crash");
		CHECK(rdp_known_host_get("host123.example.org", 3389, got) == 1 && !memcmp(got, a, 32), "read from .new");
		CHECK(rdp_known_host_set("after.example.org", 3389, a) == 0, "set after the crash");
		CHECK(access(file, F_OK) == 0 && access(tmp, F_OK) != 0, "file back, .new gone");
		CHECK(rdp_known_host_get("host200.example.org", 3389, got) == 1 && !memcmp(got, b, 32), "kept through it");
		/* a write that cannot happen leaves the file as it was */
		CHECK(mkdir(tmp, 0755) == 0, "block the .new");
		CHECK(rdp_known_host_set("blocked.example.org", 3389, a) < 0, "set fails");
		CHECK(rdp_known_host_get("host007.example.org", 3389, got) == 1, "file untouched");
		rmdir(tmp);
	}
	remove(file);
	snprintf(file, sizeof(file), "%s/sub", path);
	rmdir(file);
	rmdir(path);
	printf("%d passed, %d failed\n", passes, fails);
	return fails != 0;
}
