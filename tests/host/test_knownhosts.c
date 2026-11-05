/*
 * GFAmigaRDP host tests: the known hosts file (knownhosts.c).
 * cc -I../../src/core test_knownhosts.c ../../src/core/knownhosts.c
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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
	remove(file);
	snprintf(file, sizeof(file), "%s/sub", path);
	rmdir(file);
	rmdir(path);
	printf("%d passed, %d failed\n", passes, fails);
	return fails != 0;
}
