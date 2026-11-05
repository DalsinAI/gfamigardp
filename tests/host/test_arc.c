/*
 * Host tests: auto-reconnect cookie ([MS-RDPBCGR] 2.2.4.2/2.2.4.3, 5.5)
 * and HMAC-MD5 (RFC 2202). Clean-room from the Open Spec / RFCs.
 *   cc -I../../src/core -o test_arc test_arc.c ../../src/core/arc.c
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include <stdio.h>
#include <string.h>
#include "arc.h"

static int fails, passes;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int hex_eq(const uint8_t* a, const char* hex)
{
	size_t i, n = strlen(hex) / 2;
	for (i = 0; i < n; i++)
	{
		unsigned v;
		if (sscanf(hex + i * 2, "%2x", &v) != 1 || a[i] != (uint8_t)v)
			return 0;
	}
	return 1;
}

int main(void)
{
	uint8_t out[16], cs[28];
	struct rdp_arc_cookie c;
	uint8_t key1[16], body[64];
	int i;

	/* RFC 2202 test case 1 */
	memset(key1, 0x0b, 16);
	CHECK(rdp_hmac_md5(key1, 16, (const uint8_t*)"Hi There", 8, out) == 0, "hmac1");
	CHECK(hex_eq(out, "9294727a3638bb1c13f48ef8158bfc9d"), "rfc2202-1");

	/* RFC 2202 test case 2 */
	CHECK(rdp_hmac_md5((const uint8_t*)"Jefe", 4,
	                   (const uint8_t*)"what do ya want for nothing?", 28, out) == 0, "hmac2");
	CHECK(hex_eq(out, "750c783e6ab0b503eaa86e310a5db738"), "rfc2202-2");

	/* Annotated Save Session Info Extended (MS-RDPBCGR 4.x dump shape):
	 * infoType=3, Length, FieldsPresent=LOGON_EX_AUTORECONNECTCOOKIE,
	 * cbFieldData=28, ARC_SC with LogonId=2 and known random. */
	memset(body, 0, sizeof(body));
	body[0] = 3; /* INFOTYPE_LOGON_EXTENDED_INFO */
	body[4] = 0x26; body[5] = 0; /* Length 38 */
	body[6] = 1; /* FieldsPresent cookie */
	body[10] = 28; /* cbFieldData */
	body[14] = 28; /* ARC cbLen */
	body[18] = 1;  /* Version */
	body[22] = 2;  /* LogonId */
	for (i = 0; i < 16; i++)
		body[26 + i] = (uint8_t)(0xA0 + i);

	rdp_arc_clear(&c);
	CHECK(rdp_arc_from_save_session(&c, body, 42) == 1, "parse cookie");
	CHECK(c.have && c.logon_id == 2, "logon id");
	CHECK(c.random[0] == 0xA0 && c.random[15] == 0xAF, "random bits");

	CHECK(rdp_arc_build_cs(cs, &c) == 0, "build cs");
	CHECK(cs[0] == 28 && cs[4] == 1 && cs[8] == 2, "cs header");
	/* Enhanced Security: HMAC-MD5(random, 32 zeros) */
	{
		uint8_t z[32], expect[16];
		memset(z, 0, sizeof(z));
		CHECK(rdp_hmac_md5(c.random, 16, z, 32, expect) == 0, "verifier");
		CHECK(memcmp(cs + 12, expect, 16) == 0, "cs verifier matches");
	}

	/* Plain notify: no cookie */
	rdp_arc_clear(&c);
	body[0] = 2;
	CHECK(rdp_arc_from_save_session(&c, body, 8) == 0 && !c.have, "plain notify");

	rdp_arc_clear(&c);
	CHECK(rdp_arc_build_cs(cs, &c) < 0, "no cookie build fails");

	printf("%d passed, %d failed\n", passes, fails);
	return fails ? 1 : 0;
}
