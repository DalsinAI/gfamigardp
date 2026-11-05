/*
 * GFAmigaRDP: UTF-8 to UTF-16LE; see utf16.h.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "utf16.h"

uint32_t rdp_utf8_next(const uint8_t** pp)
{
	const uint8_t* p = *pp;
	uint32_t c = *p++;
	int extra = 0;
	if (c >= 0xf0 && c < 0xf8) { c &= 7; extra = 3; }
	else if (c >= 0xe0) { c &= 15; extra = 2; }   /* 0xf8..0xff too, as gfopenntlm reads them */
	else if (c >= 0xc0) { c &= 31; extra = 1; }
	else if (c >= 0x80) c = 0xfffd;
	while (extra--)
	{
		if ((*p & 0xc0) != 0x80)
		{
			c = 0xfffd;
			break;
		}
		c = (c << 6) | (*p++ & 63);
	}
	*pp = p;
	return c;
}

static size_t units(uint32_t c)
{
	return c >= 0x10000 && c < 0x110000 ? 4 : 2;
}

size_t rdp_utf16_len(const char* s)
{
	const uint8_t* p = (const uint8_t*)(s ? s : "");
	size_t n = 0;
	while (*p)
		n += units(rdp_utf8_next(&p));
	return n;
}

static void put16(uint8_t* o, uint32_t v)
{
	o[0] = (uint8_t)v;
	o[1] = (uint8_t)(v >> 8);
}

size_t rdp_utf16_put(const char* s, uint8_t* out, size_t size)
{
	const uint8_t* p = (const uint8_t*)(s ? s : "");
	size_t n = 0;
	while (*p)
	{
		uint32_t c = rdp_utf8_next(&p);
		if (n + units(c) > size)
			break;
		if (c >= 0x10000 && c < 0x110000)
		{
			c -= 0x10000;
			put16(out + n, 0xd800 | (c >> 10));
			put16(out + n + 2, 0xdc00 | (c & 0x3ff));
			n += 4;
		}
		else
		{
			put16(out + n, c > 0xffff ? 0xfffd : c);
			n += 2;
		}
	}
	return n;
}
