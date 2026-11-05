/*
 * GFAmigaRDP: keymap name to Windows KLID; see klid.h.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 5).
 */
#include "klid.h"

#include <stddef.h>

/* the keymaps of KEYMAPS: on AmigaOS 3.x (SetKeyboard, Input prefs) */
static const struct { const char* name; uint32_t klid; } map[] = {
	{ "usa", 0x00000409 },    /* American */
	{ "usa0", 0x00000409 },   /* American, for programs before V1.0 */
	{ "usa1", 0x00000409 },
	{ "usa2", 0x00010409 },   /* Dvorak */
	{ "gb", 0x00000809 },     /* British */
	{ "irl", 0x00001809 },    /* Irish */
	{ "cdn", 0x00001009 },    /* Canadian French */
	{ "d", 0x00000407 },      /* German */
	{ "ch1", 0x0000100c },    /* Swiss French */
	{ "ch2", 0x00000807 },    /* Swiss German */
	{ "f", 0x0000040c },      /* French */
	{ "b", 0x0000080c },      /* Belgian French */
	{ "i", 0x00000410 },      /* Italian */
	{ "e", 0x0000040a },      /* Spanish */
	{ "po", 0x00000816 },     /* Portuguese */
	{ "nl", 0x00000413 },     /* Dutch */
	{ "dk", 0x00000406 },     /* Danish */
	{ "n", 0x00000414 },      /* Norwegian */
	{ "s", 0x0000041d },      /* Swedish (and Finnish) */
	{ "is", 0x0000040f },     /* Icelandic */
	{ "pl", 0x00000415 },     /* Polish */
	{ "cz", 0x00000405 },     /* Czech */
	{ "sk", 0x0000041b },     /* Slovak */
	{ "gr", 0x00000408 },     /* Greek */
	{ "tr", 0x0000041f },     /* Turkish */
	{ "ru", 0x00000419 },     /* Russian */
};

static int same(const char* a, const char* b)
{
	for (; *a && *b; a++, b++)
	{
		char c = *a >= 'A' && *a <= 'Z' ? (char)(*a - 'A' + 'a') : *a;
		if (c != *b)
			return 0;
	}
	return !*a && !*b;
}

uint32_t rdp_klid_from_keymap(const char* name)
{
	size_t i;
	if (!name || !*name)
		return 0;
	for (i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (same(name, map[i].name))
			return map[i].klid;
	return 0;
}
