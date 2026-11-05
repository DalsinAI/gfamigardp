
/*
 * Host tests: fast-path input PDU builders ([MS-RDPBCGR] 2.2.8.1.2).
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include <stdio.h>
#include <string.h>
#include "input.h"

static int fails, passes;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
	uint8_t buf[64];
	int n, ext;
	uint8_t scan;

	n = rdp_fp_input_key(buf, sizeof(buf), 0x1E, 0, 0); /* 'A' down */
	CHECK(n == 4, "key len %d", n);
	CHECK(buf[0] == 0x04, "hdr action/num %02x", buf[0]); /* numEvents=1 << 2 */
	CHECK(buf[1] == 4, "length1");
	CHECK(buf[2] == 0x00, "eventHeader scancode");
	CHECK(buf[3] == 0x1E, "scancode");

	n = rdp_fp_input_key(buf, sizeof(buf), 0x53, 1, 1); /* Del up extended */
	CHECK(n == 4, "del len");
	CHECK(buf[2] == (uint8_t)(RDP_FP_KBD_RELEASE | RDP_FP_KBD_EXTENDED), "del flags %02x", buf[2]);

	n = rdp_fp_input_mouse(buf, sizeof(buf), RDP_PTR_MOVE, 683, 367);
	CHECK(n == 9, "mouse len %d", n);
	CHECK(buf[0] == 0x04, "mouse hdr");
	CHECK(buf[2] == (RDP_FP_EVENT_MOUSE << 5), "mouse code");
	CHECK(buf[3] == 0x00 && buf[4] == 0x08, "PTRFLAGS_MOVE");
	CHECK(buf[5] == 0xAB && buf[6] == 0x02, "x");
	CHECK(buf[7] == 0x6F && buf[8] == 0x01, "y");

	n = rdp_fp_input_cad(buf, sizeof(buf));
	CHECK(n > 0 && n == buf[1], "cad len %d", n);
	CHECK(((buf[0] >> 2) & 0x0F) == 6, "cad 6 events");

	CHECK(rdp_rawkey_to_scan(0x20, &scan, &ext) && scan == 0x1E && !ext, "amiga 'a'");
	CHECK(rdp_rawkey_to_scan(0x45, &scan, &ext) && scan == 0x01 && !ext, "amiga Esc");
	CHECK(rdp_rawkey_to_scan(0x46, &scan, &ext) && scan == 0x53 && ext, "amiga Del");
	CHECK(rdp_rawkey_to_scan(0x4C, &scan, &ext) && scan == 0x48 && ext, "amiga Up");
	CHECK(!rdp_rawkey_to_scan(0x66, &scan, &ext), "LAmiga stays local");

	printf("%d passed, %d failed\n", passes, fails);
	return fails ? 1 : 0;
}
