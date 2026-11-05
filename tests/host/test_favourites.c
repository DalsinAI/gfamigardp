/*
 * GFAmigaRDP host tests: the Favourites file's security field across the
 * file formats (markup.c ui_fav_format / ui_fav_security, review 2 N2).
 * An old (format 1) file is read line by line as fav_load reads it: a
 * favourite saved as "Network Level Authentication" must stay NLA only.
 *   cc -I../../src/ui -o test_favourites test_favourites.c ../../src/ui/markup.c
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 5).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "markup.h"

static int fails, passes;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define P_TLS 0x1u
#define P_HYBRID 0x2u

/* fav_load's loop over a file's text: the security index of each favourite */
static int load(const char* text, int sec[], int max)
{
	char buf[2048], *line, *save = NULL;
	int format = 1, n = 0;
	strncpy(buf, text, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	for (line = strtok_r(buf, "\n", &save); line && n < max; line = strtok_r(NULL, "\n", &save))
	{
		char* f = line;
		int i;
		if (ui_fav_format(line))
			format = ui_fav_format(line);
		if (line[0] == '#' || !line[0])
			continue;
		for (i = 0; i < 6 && f; i++)
			if ((f = strchr(f, '\t')))
				f++;
		sec[n++] = f ? ui_fav_security(f, format) : 0;
	}
	return n;
}

int main(void)
{
	int sec[8], n;
	/* as GFAmigaRDP wrote it before review H1 (format 1) */
	static const char old_file[] =
		"# GFAmigaRDP favourites: name, host, user, domain, resolution, colours, security\n"
		"Best\tbest.example.org\tsam\t\t1\t2\t0\n"
		"TLS\ttls.example.org\tsam\t\t1\t2\t1\n"
		"NLA\tnla.example.org\tsam\tEX\t1\t2\t2\n"
		"Odd\todd.example.org\tsam\t\t1\t2\t7\n";
	/* as it writes it now (format 2) */
	char new_file[1024];

	CHECK(ui_nsecurity == 3, "three security choices");
	n = load(old_file, sec, 8);
	CHECK(n == 4, "old file: %d favourites", n);
	CHECK(ui_security[sec[2]].protocols == (P_HYBRID | 0x8u), "old NLA only -> 0x%x", (unsigned)ui_security[sec[2]].protocols);
	CHECK(!(ui_security[sec[2]].protocols & P_TLS), "old NLA only must not allow TLS");
	CHECK(ui_security[sec[1]].protocols == P_TLS, "old TLS stays TLS");
	CHECK(!(ui_security[sec[0]].protocols & P_TLS), "old 'best' read as NLA only");
	CHECK(sec[3] == 0, "out of range -> default NLA");

	/* a header-less (hand-written) file is format 1 too */
	n = load("Hand\thand.example.org\tsam\t\t1\t2\t2\n", sec, 8);
	CHECK(n == 1 && !(ui_security[sec[0]].protocols & P_TLS), "header-less 2 is NLA only");

	/* format 2 keeps every index, "NLA, or TLS" included */
	snprintf(new_file, sizeof(new_file), "%s%s", UI_FAV_HEADER,
	         "A\ta.example.org\tsam\t\t1\t2\t0\n"
	         "B\tb.example.org\tsam\t\t1\t2\t1\n"
	         "C\tc.example.org\tsam\t\t1\t2\t2\n"
	         "D\td.example.org\tsam\t\t1\t2\t-1\n");
	CHECK(ui_fav_format(UI_FAV_HEADER) == 2, "header is format 2");
	CHECK(ui_fav_format("# GFAmigaRDP favourites: name") == 0, "old header is not");
	n = load(new_file, sec, 8);
	CHECK(n == 4, "new file: %d", n);
	CHECK(sec[0] == 0 && sec[1] == 1 && sec[2] == 2 && sec[3] == 0, "format 2: %d %d %d %d", sec[0], sec[1], sec[2], sec[3]);
	CHECK(ui_security[sec[2]].protocols == (P_TLS | P_HYBRID | 0x8u), "format 2 index 2 is NLA, or TLS");

	printf("favourites: %d passed, %d failed\n", passes, fails);
	return fails ? 1 : 0;
}
