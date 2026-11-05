/*
 * GFAmigaRDP: UTF-8 to UTF-16LE; see utf16.h.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "utf16.h"

#include <string.h>

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

int rdp_utf8_ok(const char* s)
{
	const uint8_t* p = (const uint8_t*)(s ? s : "");
	while (*p)
	{
		const uint8_t* start = p;
		uint32_t c = *p++;
		int extra = 0, minv = 0;
		if (c < 0x80)
			continue;
		if (c < 0xc2 || c >= 0xf5)
			return 0;
		if (c < 0xe0) { extra = 1; minv = 0x80; c &= 0x1f; }
		else if (c < 0xf0) { extra = 2; minv = 0x800; c &= 0x0f; }
		else { extra = 3; minv = 0x10000; c &= 0x07; }
		while (extra--)
		{
			if ((*p & 0xc0) != 0x80)
				return 0;
			c = (c << 6) | (*p++ & 0x3f);
		}
		if (c < (uint32_t)minv || (c >= 0xd800 && c <= 0xdfff) || c > 0x10ffff)
			return 0;
		(void)start;
	}
	return 1;
}

size_t rdp_latin1_to_utf8(char* dst, size_t size, const char* src)
{
	const uint8_t* p = (const uint8_t*)(src ? src : "");
	size_t n = 0;
	if (!dst || size == 0)
		return 0;
	while (*p)
	{
		uint8_t c = *p++;
		if (c < 0x80)
		{
			if (n + 1 >= size)
				break;
			dst[n++] = (char)c;
		}
		else
		{
			if (n + 2 >= size)
				break;
			dst[n++] = (char)(0xc0 | (c >> 6));
			dst[n++] = (char)(0x80 | (c & 0x3f));
		}
	}
	dst[n] = '\0';
	return n;
}

size_t rdp_amiga_to_utf8(char* dst, size_t size, const char* src)
{
	if (!dst || size == 0)
		return 0;
	if (rdp_utf8_ok(src))
	{
		rdp_copy_utf8(dst, size, src);
		return strlen(dst);
	}
	return rdp_latin1_to_utf8(dst, size, src);
}

void rdp_copy_utf8(char* dst, size_t size, const char* src)
{
	const uint8_t* p = (const uint8_t*)(src ? src : "");
	size_t n = 0;
	if (!dst || size == 0)
		return;
	while (*p)
	{
		const uint8_t* start = p;
		(void)rdp_utf8_next(&p);
		{
			size_t cl = (size_t)(p - start);
			if (n + cl >= size)
				break;
			memcpy(dst + n, start, cl);
			n += cl;
		}
	}
	dst[n] = '\0';
}

