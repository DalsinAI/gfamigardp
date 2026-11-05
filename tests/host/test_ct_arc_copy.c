/*
 * Host test: H1 CONNECT must copy arc_have / arc_logon_id / arc_random.
 * Mirrors ct_connect's field copy without Amiga exec headers.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int fails, passes;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* Subset of struct ct_msg connect fields that H1 cares about. */
struct ct_arc_params
{
	uint8_t arc_have;
	uint32_t arc_logon_id;
	uint8_t arc_random[16];
};

static void ct_copy_arc(struct ct_arc_params* dst, const struct ct_arc_params* src)
{
	dst->arc_have = src->arc_have;
	dst->arc_logon_id = src->arc_logon_id;
	if (src->arc_have)
		memcpy(dst->arc_random, src->arc_random, sizeof(dst->arc_random));
	else
		memset(dst->arc_random, 0, sizeof(dst->arc_random));
}

int main(void)
{
	struct ct_arc_params src, dst;
	int i;

	memset(&src, 0, sizeof(src));
	memset(&dst, 0xA5, sizeof(dst));
	src.arc_have = 1;
	src.arc_logon_id = 0x12345678u;
	for (i = 0; i < 16; i++)
		src.arc_random[i] = (uint8_t)(0x10 + i);

	ct_copy_arc(&dst, &src);
	CHECK(dst.arc_have == 1, "have");
	CHECK(dst.arc_logon_id == 0x12345678u, "logon_id");
	CHECK(memcmp(dst.arc_random, src.arc_random, 16) == 0, "random");

	memset(&dst, 0x5A, sizeof(dst));
	src.arc_have = 0;
	ct_copy_arc(&dst, &src);
	CHECK(dst.arc_have == 0, "no have");
	CHECK(dst.arc_logon_id == 0x12345678u, "logon_id kept"); /* copy always sets from src */
	{
		uint8_t z[16];
		memset(z, 0, 16);
		/* With have=0 the helper zeros random (matches ct_connect). */
		src.arc_logon_id = 0;
		ct_copy_arc(&dst, &src);
		CHECK(dst.arc_logon_id == 0 && memcmp(dst.arc_random, z, 16) == 0, "cleared");
	}

	printf("%d passed, %d failed\n", passes, fails);
	return fails ? 1 : 0;
}
