/*
 * GFAmigaRDP: the X.224 request and confirm against byte strings worked
 * out by hand from [MS-RDPBCGR] 2.2.1.1 and 2.2.1.2. Plain C: runs on a
 * PC (cc) and on the Amiga (X224Test, or under vamos).
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "x224.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); fails++; } } while (0)

int main(void)
{
	uint8_t buf[256];
	struct rdp_x224_cc cc;
	int n;

	/* no cookie, TLS and NLA asked for: 4 + 7 + 8 = 19 bytes */
	static const uint8_t cr_plain[] = { 0x03, 0x00, 0x00, 0x13, 0x0E, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00,
	                                    0x01, 0x00, 0x08, 0x00, 0x03, 0x00, 0x00, 0x00 };
	n = rdp_x224_build_cr(buf, sizeof(buf), NULL, RDP_PROTOCOL_SSL | RDP_PROTOCOL_HYBRID, 0);
	CHECK(n == (int)sizeof(cr_plain));
	CHECK(n > 0 && !memcmp(buf, cr_plain, sizeof(cr_plain)));

	/* with the cookie "Cookie: mstshash=sam\r\n" (22 bytes): 41 bytes, LI 36 */
	n = rdp_x224_build_cr(buf, sizeof(buf), "sam", RDP_PROTOCOL_SSL, 0);
	CHECK(n == 41);
	CHECK(buf[3] == 41 && buf[4] == 36 && buf[5] == 0xE0);
	CHECK(!memcmp(buf + 11, "Cookie: mstshash=sam\r\n", 22));
	CHECK(buf[33] == 0x01 && buf[35] == 0x08 && buf[37] == 0x01);

	/* a cookie with a line break, and too small a buffer */
	CHECK(rdp_x224_build_cr(buf, sizeof(buf), "a\r\nb", 0, 0) == RDP_E_ARG);
	CHECK(rdp_x224_build_cr(buf, 10, NULL, 0, 0) == RDP_E_SPACE);

	/* the confirm: RDP_NEG_RSP, TLS chosen, extended client data */
	{
		static const uint8_t cc_rsp[] = { 0x03, 0x00, 0x00, 0x13, 0x0E, 0xD0, 0x00, 0x00, 0x12, 0x34, 0x00,
		                                  0x02, 0x01, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00 };
		CHECK(rdp_x224_parse_cc(cc_rsp, sizeof(cc_rsp), &cc) == 0);
		CHECK(cc.has_neg && cc.neg_type == RDP_NEG_RSP && cc.selected == RDP_PROTOCOL_SSL);
		CHECK(cc.neg_flags == RDP_NEGRSP_EXTENDED_CLIENT_DATA_SUPPORTED);
	}
	/* RDP_NEG_FAILURE: NLA wanted */
	{
		static const uint8_t cc_fail[] = { 0x03, 0x00, 0x00, 0x13, 0x0E, 0xD0, 0x00, 0x00, 0x00, 0x00, 0x00,
		                                   0x03, 0x00, 0x08, 0x00, 0x05, 0x00, 0x00, 0x00 };
		CHECK(rdp_x224_parse_cc(cc_fail, sizeof(cc_fail), &cc) == 0);
		CHECK(cc.neg_type == RDP_NEG_FAILURE && cc.failure == RDP_NEGFAIL_HYBRID_REQUIRED_BY_SERVER);
	}
	/* an old server: no negotiation data, standard RDP security */
	{
		static const uint8_t cc_old[] = { 0x03, 0x00, 0x00, 0x0B, 0x06, 0xD0, 0x00, 0x00, 0x00, 0x00, 0x00 };
		CHECK(rdp_x224_parse_cc(cc_old, sizeof(cc_old), &cc) == 0);
		CHECK(!cc.has_neg && cc.selected == RDP_PROTOCOL_RDP);
	}
	/* not a confirm; a short LI; a TPKT length that disagrees */
	{
		uint8_t bad[19];
		static const uint8_t cc_rsp[] = { 0x03, 0x00, 0x00, 0x13, 0x0E, 0xD0, 0x00, 0x00, 0x00, 0x00, 0x00,
		                                  0x02, 0x00, 0x08, 0x00, 0x02, 0x00, 0x00, 0x00 };
		memcpy(bad, cc_rsp, sizeof(bad));
		bad[5] = 0xE0;
		CHECK(rdp_x224_parse_cc(bad, sizeof(bad), &cc) == RDP_E_PROTO);
		memcpy(bad, cc_rsp, sizeof(bad));
		bad[4] = 0x09;
		CHECK(rdp_x224_parse_cc(bad, sizeof(bad), &cc) == RDP_E_PROTO);
		memcpy(bad, cc_rsp, sizeof(bad));
		bad[3] = 0x20;
		CHECK(rdp_x224_parse_cc(bad, sizeof(bad), &cc) == RDP_E_PROTO);
		CHECK(rdp_x224_parse_cc(cc_rsp, sizeof(cc_rsp), &cc) == 0 && cc.selected == RDP_PROTOCOL_HYBRID);
	}

	printf("x224: %s (%d failed)\n", fails ? "FAIL" : "ok", fails);
	return fails ? 20 : 0;
}
