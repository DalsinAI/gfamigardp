/*
 * GFAmigaRDP host tests: Amiga keymap name to Windows KLID (klid.c,
 * review 2 N7), a table of the KEYMAPS: names.
 *   cc -I../../src/core -o test_klid test_klid.c ../../src/core/klid.c
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 5).
 */
#include <stdio.h>
#include <stdint.h>
#include "klid.h"

static int fails, passes;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
	static const struct { const char* name; uint32_t klid; } want[] = {
		{ "usa", 0x00000409 }, { "usa0", 0x00000409 }, { "usa2", 0x00010409 },
		{ "gb", 0x00000809 }, { "irl", 0x00001809 }, { "cdn", 0x00001009 },
		{ "d", 0x00000407 }, { "ch1", 0x0000100c }, { "ch2", 0x00000807 },
		{ "f", 0x0000040c }, { "b", 0x0000080c }, { "i", 0x00000410 },
		{ "e", 0x0000040a }, { "po", 0x00000816 }, { "nl", 0x00000413 },
		{ "dk", 0x00000406 }, { "n", 0x00000414 }, { "s", 0x0000041d },
		{ "is", 0x0000040f }, { "pl", 0x00000415 }, { "cz", 0x00000405 },
		{ "sk", 0x0000041b }, { "gr", 0x00000408 }, { "tr", 0x0000041f },
		{ "ru", 0x00000419 },
		/* case does not matter */
		{ "GB", 0x00000809 }, { "D", 0x00000407 }, { "Usa", 0x00000409 },
		/* unknown or nothing: 0, the locale's country decides */
		{ "", 0 }, { "xx", 0 }, { "gbr", 0 }, { "g", 0 }, { "usa22", 0 }, { "d ", 0 },
	};
	unsigned i;

	for (i = 0; i < sizeof(want) / sizeof(want[0]); i++)
	{
		uint32_t got = rdp_klid_from_keymap(want[i].name);
		CHECK(got == want[i].klid, "\"%s\": 0x%08lx, not 0x%08lx", want[i].name,
		      (unsigned long)got, (unsigned long)want[i].klid);
	}
	CHECK(rdp_klid_from_keymap(NULL) == 0, "NULL");
	printf("klid: %d passed, %d failed\n", passes, fails);
	return fails ? 1 : 0;
}
