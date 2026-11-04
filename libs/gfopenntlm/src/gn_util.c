/* gfopenntlm: byte order helpers and UTF-8 to UTF-16LE.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gn_internal.h"

void gn_put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void gn_put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
uint16_t gn_get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t gn_get32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void gn_wipe(void *p, size_t n)
{
	volatile uint8_t *q = p;
	while (n--) *q++ = 0;
}

/* Upper case for the Basic Latin and Latin-1 letters; [MS-NLMP] 3.3.2's
 * Uppercase() is the Unicode mapping, which agrees on these. */
static uint32_t upper(uint32_t c)
{
	if (c >= 'a' && c <= 'z') return c - 32;
	if (c >= 0xe0 && c <= 0xfe && c != 0xf7) return c - 32;
	if (c == 0xff) return 0x178;
	return c;
}

uint8_t *gn_utf16(const char *s, int up, size_t *outlen)
{
	const uint8_t *p = (const uint8_t *)(s ? s : "");
	size_t n = 0, cap;
	uint8_t *out;
	while (p[n]) n++;
	cap = 4 * n + 2;            /* each byte gives at most one unit, a surrogate pair at most 4 */
	out = gn_alloc(cap);
	if (!out) return NULL;
	n = 0;
	while (*p) {
		uint32_t c = *p++;
		int extra = 0;
		if (c >= 0xf0 && c < 0xf8) { c &= 7; extra = 3; }
		else if (c >= 0xe0) { c &= 15; extra = 2; }
		else if (c >= 0xc0) { c &= 31; extra = 1; }
		else if (c >= 0x80) c = 0xfffd;
		while (extra--) {
			if ((*p & 0xc0) != 0x80) { c = 0xfffd; break; }
			c = (c << 6) | (*p++ & 63);
		}
		if (up) c = upper(c);
		if (c >= 0x10000 && c < 0x110000) {
			c -= 0x10000;
			gn_put16(out + n, (uint16_t)(0xd800 | (c >> 10)));
			gn_put16(out + n + 2, (uint16_t)(0xdc00 | (c & 0x3ff)));
			n += 4;
		} else {
			gn_put16(out + n, (uint16_t)(c > 0xffff ? 0xfffd : c));
			n += 2;
		}
	}
	*outlen = n;
	return out;
}
