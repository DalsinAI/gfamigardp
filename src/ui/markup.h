/*
 * GFAmigaRDP: the windows' markup and CSS, the connection choices and the
 * test pattern (markup.c). Plain C: no Amiga types.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_UI_MARKUP_H
#define RDP_UI_MARKUP_H

#include <stdint.h>

struct ui_resolution { const char* text; uint16_t w, h; };   /* 0 x 0: the screen's size */
struct ui_depth { const char* text; uint8_t bits; };
struct ui_security { const char* text; uint32_t protocols; }; /* RDP_PROTOCOL_* */
extern const struct ui_resolution ui_resolutions[];
extern const struct ui_depth ui_depths[];
extern const struct ui_security ui_security[];
extern const int ui_nresolutions, ui_ndepths, ui_nsecurity;

const char* ui_connect_markup(void);
extern const char ui_connect_css[];
extern const char ui_toolbar_markup[], ui_toolbar_css[];
extern const char ui_status_markup[], ui_status_css[];

/* the test pattern's colour (0xRRGGBB) at x, y of a W x H desktop */
uint32_t ui_pattern(int x, int y, int W, int H);

#endif
