/*
 * GFAmigaRDP: fast-path input PDUs and Amiga rawkey → PC set-1 map.
 * Clean room from [MS-RDPBCGR] 2.2.8.1.2; Amiga rawkeys from the OS docs.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "input.h"

#include <string.h>

/* PC set-1 scancode in bits 0..7; bit 8 set = EXTENDED. 0 = unmapped. */
static const uint16_t s_raw_to_scan[128] = {
	/* 0x00..0x0F: ` 1 2 3 4 5 6 7 8 9 0 - = \ BSP */
	0x0029, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008,
	0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x002B, 0x0000, 0x0000,
	/* 0x10..0x1F: q w e r t y u i o p [ ] */
	0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
	0x0018, 0x0019, 0x001A, 0x001B, 0x0000, 0x0000, 0x0000, 0x0000,
	/* 0x20..0x2F: a s d f g h j k l ; ' */
	0x001E, 0x001F, 0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025,
	0x0026, 0x0027, 0x0028, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	/* 0x30..0x3F: <>? z x c v b n m , . / */
	0x0056, 0x002C, 0x002D, 0x002E, 0x002F, 0x0030, 0x0031, 0x0032,
	0x0033, 0x0034, 0x0035, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	/* 0x40..0x4F: space BSP tab enter ret del help  ... cursors */
	0x0039, 0x000E, 0x000F, 0x001C, 0x001C, 0x0001, 0x0153, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0148, 0x0150, 0x014D, 0x014B,
	/* 0x50..0x5F: F1..F10  ... */
	0x003B, 0x003C, 0x003D, 0x003E, 0x003F, 0x0040, 0x0041, 0x0042,
	0x0043, 0x0044, 0x0057, 0x0058, 0x0000, 0x0000, 0x0000, 0x0000,
	/* 0x60..0x6F: LShift RShift Caps Ctrl LAlt RAlt LAmi RAmi */
	0x002A, 0x0036, 0x003A, 0x001D, 0x0038, 0x0138, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	/* 0x70..0x7F: keypad-ish / wheel often 0x7A/0x7B (handled as mouse) */
	0x0052, 0x004F, 0x0050, 0x0051, 0x004B, 0x004C, 0x004D, 0x0047,
	0x0048, 0x0049, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

int rdp_rawkey_to_scan(uint16_t raw, uint8_t* scan, int* ext)
{
	uint16_t v;
	raw &= 0x7F;
	v = s_raw_to_scan[raw];
	if (!v)
		return 0;
	if (scan)
		*scan = (uint8_t)(v & 0xFF);
	if (ext)
		*ext = (v & 0x100) ? 1 : 0;
	return 1;
}

static int put_event(uint8_t* p, size_t room, const struct rdp_fp_event* e)
{
	if (e->code == RDP_FP_EVENT_SCANCODE)
	{
		if (room < 2)
			return -1;
		p[0] = (uint8_t)((RDP_FP_EVENT_SCANCODE << 5) | (e->flags & 0x1F));
		p[1] = e->scancode;
		return 2;
	}
	if (e->code == RDP_FP_EVENT_MOUSE)
	{
		if (room < 7)
			return -1;
		p[0] = (uint8_t)(RDP_FP_EVENT_MOUSE << 5);
		p[1] = (uint8_t)(e->ptr_flags);
		p[2] = (uint8_t)(e->ptr_flags >> 8);
		p[3] = (uint8_t)(e->x);
		p[4] = (uint8_t)(e->x >> 8);
		p[5] = (uint8_t)(e->y);
		p[6] = (uint8_t)(e->y >> 8);
		return 7;
	}
	if (e->code == RDP_FP_EVENT_UNICODE)
	{
		if (room < 3)
			return -1;
		p[0] = (uint8_t)((RDP_FP_EVENT_UNICODE << 5) | (e->flags & 0x1F));
		p[1] = (uint8_t)(e->unicode);
		p[2] = (uint8_t)(e->unicode >> 8);
		return 3;
	}
	return -1;
}

int rdp_fp_input_build(uint8_t* out, size_t out_size, const struct rdp_fp_event* ev, int n)
{
	size_t pos;
	int i;
	if (!out || !ev || n < 1 || n > 15)
		return -1;
	/* header + length1 + events; length filled after */
	if (out_size < 2)
		return -1;
	/* action=0, numEvents=n, flags=0 (TLS / ENCRYPTION_LEVEL_NONE) */
	out[0] = (uint8_t)((n & 0x0F) << 2);
	pos = 2;
	for (i = 0; i < n; i++)
	{
		int k = put_event(out + pos, out_size - pos, &ev[i]);
		if (k < 0)
			return -1;
		pos += (size_t)k;
	}
	if (pos > 127)
	{
		/* two-byte length: rare for our small PDUs; keep simple and fail */
		return -1;
	}
	out[1] = (uint8_t)pos;
	return (int)pos;
}

int rdp_fp_input_key(uint8_t* out, size_t out_size, uint8_t scancode, int extended, int release)
{
	struct rdp_fp_event e;
	memset(&e, 0, sizeof(e));
	e.code = RDP_FP_EVENT_SCANCODE;
	e.flags = (uint8_t)((release ? RDP_FP_KBD_RELEASE : 0) | (extended ? RDP_FP_KBD_EXTENDED : 0));
	e.scancode = scancode;
	return rdp_fp_input_build(out, out_size, &e, 1);
}

int rdp_fp_input_mouse(uint8_t* out, size_t out_size, uint16_t ptr_flags, uint16_t x, uint16_t y)
{
	struct rdp_fp_event e;
	memset(&e, 0, sizeof(e));
	e.code = RDP_FP_EVENT_MOUSE;
	e.ptr_flags = ptr_flags;
	e.x = x;
	e.y = y;
	return rdp_fp_input_build(out, out_size, &e, 1);
}

int rdp_fp_input_cad(uint8_t* out, size_t out_size)
{
	struct rdp_fp_event e[6];
	memset(e, 0, sizeof(e));
	/* Ctrl down, Alt down, Del down, Del up, Alt up, Ctrl up */
	e[0].code = RDP_FP_EVENT_SCANCODE; e[0].scancode = 0x1D;
	e[1].code = RDP_FP_EVENT_SCANCODE; e[1].scancode = 0x38;
	e[2].code = RDP_FP_EVENT_SCANCODE; e[2].scancode = 0x53; e[2].flags = RDP_FP_KBD_EXTENDED;
	e[3].code = RDP_FP_EVENT_SCANCODE; e[3].scancode = 0x53; e[3].flags = RDP_FP_KBD_EXTENDED | RDP_FP_KBD_RELEASE;
	e[4].code = RDP_FP_EVENT_SCANCODE; e[4].scancode = 0x38; e[4].flags = RDP_FP_KBD_RELEASE;
	e[5].code = RDP_FP_EVENT_SCANCODE; e[5].scancode = 0x1D; e[5].flags = RDP_FP_KBD_RELEASE;
	return rdp_fp_input_build(out, out_size, e, 6);
}
