/*
 * GFAmigaRDP host tests: utf16.c against gfopenntlm's gn_utf16 (the Client
 * Info and NLA must send the same UTF-16LE), and the session PDUs (pdu.c).
 * cc -I../../src/core -I$GFOPENNTLM/src test_utf16_pdu.c ../../src/core/utf16.c
 *    ../../src/core/pdu.c $GFOPENNTLM/src/gn_util.c
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utf16.h"
#include "pdu.h"
#include "gn_internal.h"

/* gn_util.c's allocator, which the library has in gn_mem.c */
void* gn_alloc(size_t n) { return calloc(1, n ? n : 1); }
void gn_free(void* p, size_t n) { if (p && n) memset(p, 0, n); free(p); }

static int fails, passes;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void same(const char* s)
{
	size_t gl = 0, l = rdp_utf16_len(s), n;
	uint8_t* g = gn_utf16(s, 0, &gl);
	uint8_t* m = malloc(l + 4);
	n = rdp_utf16_put(s, m, l);
	CHECK(g && gl == l && n == l && !memcmp(g, m, l), "utf16 differs for %zu-byte input (gn %zu, rdp %zu/%zu)", strlen(s), gl, l, n);
	free(g);
	free(m);
}

static uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t* p) { return u16(p) | (uint32_t)u16(p + 2) << 16; }

int main(void)
{
	static const char* cases[] = {
		"", "da1ek", "Passw0rd-123", "J\xC3\xBCrgen", "\xE2\x82\xAC" "100", "\xF0\x9F\x98\x80 smile",
		"bad \x80 cont", "trunc \xC3", "\xF8\x88\x80\x80\x80", "\xFF\xFE", "\xED\xA0\x80", "mixed \xC3\x28 x",
		"\xE2\x82", "\xF4\x8F\xBF\xBF", "\xF5\x80\x80\x80",
	};
	size_t i;
	int c;
	char one[2] = { 0, 0 };
	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
		same(cases[i]);
	for (c = 1; c < 256; c++) { one[0] = (char)c; same(one); }
	CHECK(rdp_utf16_len("J\xC3\xBCrgen") == 12, "len");
	CHECK(rdp_utf16_len("\xF0\x9F\x98\x80") == 4, "surrogate len");

	/* Confirm Active: the lengths in its headers and capability sets add up */
	{
		uint8_t buf[1024];
		struct rdp_stream s;
		struct rdp_demand d;
		size_t pos, off, ncaps, k;
		uint16_t total, lsrc, lcomb;
		memset(&d, 0, sizeof(d));
		d.share_id = 0x103EA;
		rdp_s_init(&s, buf, sizeof(buf));
		CHECK(rdp_build_confirm_active(&s, &d, 1007, 1024, 768, 16) > 0, "build");
		total = u16(buf);
		CHECK(total == s.pos, "totalLength %u vs %zu", total, s.pos);
		CHECK((u16(buf + 2) & 0x0F) == RDP_PDUTYPE_CONFIRMACTIVE, "type");
		CHECK(u16(buf + 4) == 1007, "pduSource");
		CHECK(u32(buf + 6) == 0x103EA, "shareId");
		CHECK(u16(buf + 10) == RDP_SERVER_CHANNEL_ID, "originatorId");
		lsrc = u16(buf + 12);
		lcomb = u16(buf + 14);
		CHECK(lsrc == 6 && !memcmp(buf + 16, "MSTSC", 6), "sourceDescriptor");
		off = 16 + lsrc;
		ncaps = u16(buf + off);
		CHECK(ncaps == (size_t)rdp_client_ncaps, "ncaps %zu", ncaps);
		CHECK(16 + lsrc + lcomb == total, "lengthCombinedCapabilities");
		pos = off + 4;
		for (k = 0; k < ncaps && pos + 4 <= total; k++)
		{
			CHECK(u16(buf + pos) == rdp_client_cap_types[k], "cap %zu type", k);
			if (u16(buf + pos) == 2)   /* bitmap */
				CHECK(u16(buf + pos + 12) == 1024 && u16(buf + pos + 14) == 768 && u16(buf + pos + 4) == 16, "bitmap cap");
			pos += u16(buf + pos + 2);
		}
		CHECK(k == ncaps && pos == total, "caps end at %zu of %u", pos, total);
	}
	/* a data PDU: uncompressedLength is totalLength - 14 ([MS-RDPBCGR] 2.2.8.1.1.1.2) */
	{
		uint8_t buf[64], p[8];
		struct rdp_stream s;
		struct rdp_share sh;
		rdp_payload_control(p, RDP_CTRLACTION_COOPERATE);
		rdp_s_init(&s, buf, sizeof(buf));
		CHECK(rdp_build_data(&s, 0x103EA, 1007, RDP_PDUTYPE2_CONTROL, p, 8) > 0, "data");
		CHECK(u16(buf) == s.pos && u16(buf + 12) == s.pos - 14, "data lengths %u %u", u16(buf), u16(buf + 12));
		CHECK(rdp_share_parse(buf, s.pos, &sh) == 0 && sh.type == RDP_PDUTYPE_DATA &&
		      sh.type2 == RDP_PDUTYPE2_CONTROL && sh.action == RDP_CTRLACTION_COOPERATE && sh.share_id == 0x103EA, "parse back");
	}
	printf("%d passed, %d failed\n", passes, fails);
	return fails != 0;
}
