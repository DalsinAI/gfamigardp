/*
 * GFAmigaRDP: the windows' markup and CSS (OpenLayout UI), the choices
 * the connection window offers, and the desktop's test pattern. Plain C,
 * so tools/preview can lay the same windows out on a PC.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "markup.h"

#include <stdio.h>
#include <string.h>

const struct ui_resolution ui_resolutions[] = {
	{ "640 \xC3\x97 480", 640, 480 },
	{ "800 \xC3\x97 600", 800, 600 },
	{ "1024 \xC3\x97 768", 1024, 768 },
	{ "1280 \xC3\x97 1024", 1280, 1024 },
	{ "The Workbench screen's size", 0, 0 },
};
const struct ui_depth ui_depths[] = {
	{ "256 colours (8-bit)", 8 },
	{ "32,768 colours (15-bit)", 15 },
	{ "65,536 colours (16-bit)", 16 },
	{ "16.7 million colours (24-bit)", 24 },
};
/* RDP_PROTOCOL_SSL 1, RDP_PROTOCOL_HYBRID 2 (core/x224.h) */
const struct ui_security ui_security[] = {
	{ "The best the computer offers", 0x1 | 0x2 | 0x8 },   /* TLS, HYBRID, HYBRID_EX */
	{ "TLS", 0x1 },
	{ "Network Level Authentication", 0x2 | 0x8 },         /* HYBRID, HYBRID_EX */
};
const int ui_nresolutions = sizeof(ui_resolutions) / sizeof(ui_resolutions[0]);
const int ui_ndepths = sizeof(ui_depths) / sizeof(ui_depths[0]);
const int ui_nsecurity = sizeof(ui_security) / sizeof(ui_security[0]);

static const char markup_head[] =
	"<column id=main>"
	"<row id=body>"
	"<column id=left>"
	"<fieldset><legend>Computer</legend>"
	"<row><label for=host>Co<u>m</u>puter</label><input id=host placeholder='Name or address' maxlength=255></row>"
	"<row><label for=user><u>U</u>ser name</label><input id=user maxlength=63></row>"
	"<row><label for=domain><u>D</u>omain</label><input id=domain placeholder='If it has one' maxlength=63></row>"
	"<row><label for=password><u>P</u>assword</label><input id=password type=password maxlength=127></row>"
	"</fieldset>"
	"<fieldset><legend>Display</legend>";
static const char markup_tail[] =
	"</fieldset>"
	"</column>"
	"<fieldset id=favbox><legend><u>F</u>avourites</legend>"
	"<list id=favs></list>"
	"<row><button id=save><span><u>S</u>ave</span></button><button id=delete><span>D<u>e</u>lete</span></button></row>"
	"</fieldset>"
	"</row>"
	"<row id=buttons><button id=connect><span><u>C</u>onnect</span></button><spacer></spacer>"
	"<button id=cancel><span>C<u>a</u>ncel</span></button></row>"
	"<p id=status>Ready.</p>"
	"</column>";

const char ui_connect_css[] =
	"#main{flex:1} #body{flex:1} #left{flex:1}"
	"label{width:84px} input,select{flex:1}"
	"#favbox{width:190px} #favs{flex:1}"
	"#save,#delete{flex:1} #connect,#cancel{width:90px}"
	"#status{margin-top:2px}";

const char* ui_connect_markup(void)
{
	static char buf[4096];
	char* p;
	int i;

	strcpy(buf, markup_head);
	p = buf + strlen(buf);
	p += sprintf(p, "<row><label for=res><u>R</u>esolution</label><select id=res>");
	for (i = 0; i < ui_nresolutions; i++)
		p += sprintf(p, "<option value=%d>%s</option>", i, ui_resolutions[i].text);
	p += sprintf(p, "</select></row><row><label for=depth>Co<u>l</u>ours</label><select id=depth>");
	for (i = 0; i < ui_ndepths; i++)
		p += sprintf(p, "<option value=%d>%s</option>", i, ui_depths[i].text);
	p += sprintf(p, "</select></row><row><label for=sec>Securit<u>y</u></label><select id=sec>");
	for (i = 0; i < ui_nsecurity; i++)
		p += sprintf(p, "<option value=%d>%s</option>", i, ui_security[i].text);
	p += sprintf(p, "</select></row>");
	strcpy(p, markup_tail);
	return buf;
}

const char ui_toolbar_markup[] =
	"<row id=tb>"
	"<button id=disc title='Close the session (Amiga-D)'>Disconnect</button>"
	"<button id=cad title='Send Ctrl-Alt-Del to the computer'>Ctrl-Alt-Del</button>"
	"<button id=full title='The session on a screen of its own (Amiga-F)'>Full screen</button>"
	"<button id=keys title='Which keys go to the computer'>Keys...</button>"
	"<spacer></spacer><label id=where></label>"
	"</row>";
const char ui_toolbar_css[] = "#tb{flex:1;padding:2px} button{padding:1px 8px;white-space:nowrap} label{white-space:nowrap}";

const char ui_status_markup[] =
	"<row id=sb><label id=status></label><spacer></spacer><label id=mode></label></row>";
const char ui_status_css[] = "#sb{flex:1;padding:1px 4px} label{white-space:nowrap}";

static uint32_t lerp(uint32_t a, uint32_t b, int t, int n) /* a + (b - a) * t / n per channel */
{
	uint32_t r = 0;
	int sh;
	for (sh = 0; sh <= 16; sh += 8)
	{
		int ca = (a >> sh) & 255, cb = (b >> sh) & 255;
		r |= (uint32_t)(ca + (cb - ca) * t / (n ? n : 1)) << sh;
	}
	return r;
}

/* a mock window: a frame, a title bar and a light client area */
static int in_box(int x, int y, int bx, int by, int bw, int bh, uint32_t* c)
{
	if (x < bx || y < by || x >= bx + bw || y >= by + bh)
		return 0;
	if (x == bx || y == by || x == bx + bw - 1 || y == by + bh - 1)
		*c = 0x202020;
	else if (y < by + 18)
		*c = lerp(0x2050A0, 0x3080D0, x - bx, bw);
	else
		*c = ((x - bx) / 8 + (y - by) / 8) % 7 == 0 ? 0xE8E8F0 : 0xF4F4F8;
	return 1;
}

uint32_t ui_pattern(int x, int y, int W, int H)
{
	uint32_t c;
	int bars = H - H / 6, ramp = bars - H / 24;

	if (y >= bars)
	{
		static const uint32_t col[8] = { 0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x000000 };
		return col[(x * 8) / (W ? W : 1)];
	}
	if (y >= ramp)
	{
		int g = (x * 255) / (W ? W : 1);
		g &= ~15; /* 16 steps: banding shows the depth */
		return (uint32_t)g * 0x010101;
	}
	if (in_box(x, y, W / 12, H / 10, W / 2, H / 2, &c))
		return c;
	if (in_box(x, y, W / 2, H / 3, W / 3 + W / 12, H / 3, &c))
		return c;
	if ((x & 63) == 0 || (y & 63) == 0)
		return 0x5A7AA0;
	return lerp(lerp(0x102850, 0x1A5A6A, x, W), lerp(0x183060, 0x0A3A40, x, W), y, ramp);
}
