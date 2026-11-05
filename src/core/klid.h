/*
 * GFAmigaRDP: the Windows keyboard layout (KLID) for an Amiga keymap name
 * (keymap.library's default keymap, e.g. "gb", "d", "usa"), review 2 N7.
 * Plain C, so the host tests run it too.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 5).
 */
#ifndef RDP_KLID_H
#define RDP_KLID_H

#include <stdint.h>

/* the KLID for keymap name (case does not matter), or 0 when it is not
 * known (or name is NULL or empty): then the locale's country is used */
uint32_t rdp_klid_from_keymap(const char* name);

#endif
