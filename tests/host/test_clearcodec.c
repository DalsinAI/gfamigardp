/*
 * GFAmigaRDP host tests: ClearCodec ([MS-RDPEGFX] 2.2.4.1, examples 4.1.1.1-4).
 * Vectors are the specification's own network dumps (CLEAN-ROOM.md).
 *   cc -I../../src/core -I../../src/codec -o test_clearcodec test_clearcodec.c \
 *      ../../src/codec/clearcodec.c ../../src/codec/gfx_fb.c
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clearcodec.h"
#include "rdpgfx.h"
#include "stream.h"

static int fails, passes;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* MS-RDPEGFX 4.1.1.2: 78x17 RLEX-only stream */
static const uint8_t ex2[] = {
	0x00, 0x0d,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x82, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x4e, 0x00, 0x11, 0x00, 0x75, 0x00, 0x00, 0x00, 0x02,
	0x0e, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0xdb, 0xff, 0xff, 0x00, 0x3a, 0x90,
	0xff, 0xb6, 0x66, 0x66, 0xb6, 0xff, 0xb6, 0x66, 0x00, 0x90, 0xdb, 0xff, 0x00, 0x00, 0x3a,
	0xdb, 0x90, 0x3a, 0x3a, 0x90, 0xdb, 0x66, 0x00, 0x00, 0xff, 0xff, 0xb6, 0x64, 0x64, 0x64,
	0x11, 0x04, 0x11, 0x4c, 0x11, 0x4c, 0x11, 0x4c, 0x11, 0x4c, 0x11, 0x4c, 0x00, 0x47, 0x13, 0x00,
	0x01, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x47, 0x16, 0x00, 0x11, 0x02, 0x00, 0x47, 0x29, 0x00,
	0x11, 0x01, 0x00, 0x49, 0x0a, 0x00, 0x01, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00, 0x4a, 0x0a, 0x00,
	0x09, 0x00, 0x01, 0x00, 0x00, 0x47, 0x05, 0x00, 0x01, 0x01, 0x1c, 0x00, 0x01, 0x00, 0x11, 0x4c,
	0x11, 0x4c, 0x11, 0x4c, 0x00, 0x47, 0x0d, 0x4d, 0x00, 0x4d
};

/* 4.1.1.3: 64x24 residual + bands (bands are cache hits — needs prior state).
 * Residual alone fills the whole image; we test residual then a synthetic miss. */
static const uint8_t ex3_hdr_residual[] = {
	0x00, 0xdf,
	0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xfe, 0xfe, 0xfe, 0xff, 0x80, 0x05, 0xff, 0xff, 0xff, 0x40, 0xfe, 0xfe, 0xfe, 0x40
};

/* 4.1.1.4: 7x15 glyph-index + short V-Bar miss then hits */
static const uint8_t ex4[] = {
	0x01, 0x0b, 0x78, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xb6, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xb6, 0x66, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xb6, 0x66, 0xdb, 0x90, 0x3a, 0xff, 0xff, 0xb6, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0x46, 0x91, 0x47, 0x91, 0x48, 0x91, 0x49, 0x91, 0x4a, 0x91, 0x1b, 0x91
};

/* 4.1.1.1: glyph hit only */
static const uint8_t ex1[] = { 0x03, 0xc3, 0x11, 0x00 };

static void test_residual_fill(void)
{
	struct rdp_clearcodec cc;
	uint32_t* px = calloc(64 * 24, 4);
	int rc;
	rdp_clearcodec_init(&cc); CHECK(rdp_clearcodec_alloc(&cc, NULL) == 0, "clear alloc");
	CHECK(px, "nomem");
	rc = rdp_clearcodec_decode(&cc, NULL, ex3_hdr_residual, sizeof(ex3_hdr_residual),
	                           px, 64, 64, 24);
	CHECK(rc == 0, "ex3 residual rc %d", rc);
	CHECK(px[0] == 0xFFFEFEFEu, "ex3 [0]=%08x", px[0]);
	CHECK(px[1407] == 0xFFFEFEFEu, "ex3 [1407]");
	CHECK(px[1408] == 0xFFFFFFFFu, "ex3 [1408]=%08x", px[1408]);
	CHECK(px[1471] == 0xFFFFFFFFu, "ex3 [1471]");
	CHECK(px[1472] == 0xFFFEFEFEu, "ex3 [1472]");
	CHECK(px[1535] == 0xFFFEFEFEu, "ex3 last");
	CHECK(cc.st.residual == 1 && cc.st.streams == 1, "ex3 stats");
	rdp_clearcodec_free(&cc, NULL);
	free(px);
}

static void test_rlex_ex2(void)
{
	struct rdp_clearcodec cc;
	uint32_t* px = calloc(78 * 17, 4);
	int rc;
	rdp_clearcodec_init(&cc); CHECK(rdp_clearcodec_alloc(&cc, NULL) == 0, "clear alloc");
	CHECK(px, "nomem");
	/* first message expects seq 0; dump has 13 — allow resync */
	rc = rdp_clearcodec_decode(&cc, NULL, ex2, sizeof(ex2), px, 78, 78, 17);
	CHECK(rc == 0, "ex2 rc %d", rc);
	/* first segment: five white then black */
	CHECK(px[0] == 0xFFFFFFFFu, "ex2 px0 %08x", px[0]);
	CHECK(px[4] == 0xFFFFFFFFu, "ex2 px4");
	CHECK(px[5] == 0xFF000000u, "ex2 px5 %08x", px[5]);
	CHECK(cc.st.sub_rlex == 1, "ex2 rlex count");
	CHECK(cc.st.streams == 1, "ex2 streams");
	rdp_clearcodec_free(&cc, NULL);
	free(px);
}

/* test-only: put BGR column into a V-Bar slot */
static int vbar_store_test(struct rdp_clearcodec* cc, uint16_t idx, const uint8_t* bgr, uint8_t h)
{
	uint8_t* n;
	size_t bytes = (size_t)h * 3;
	if (idx >= RDP_CLEAR_VBAR_SLOTS)
		return -1;
	free(cc->vbar[idx].bgr);
	n = (uint8_t*)malloc(bytes);
	if (!n)
		return -1;
	memcpy(n, bgr, bytes);
	cc->vbar[idx].bgr = n;
	cc->vbar[idx].h = h;
	return 0;
}

static void test_glyph_and_vbar_ex4(void)
{
	struct rdp_clearcodec cc;
	uint32_t* px = calloc(7 * 15, 4);
	uint32_t* px2 = calloc(2, 4);
	int rc, i;
	/* One-column band: SHORT_VBAR_CACHE_MISS only (from ex4's first V-Bar). */
	static const uint8_t miss_only[] = {
		0x00, 0x0b, /* flags=0, seq=11 */
		0x00, 0x00, 0x00, 0x00, /* residual */
		0x3a, 0x00, 0x00, 0x00, /* bands = 58: 11 hdr + 2 + 45 pixels */
		0x00, 0x00, 0x00, 0x00, /* sub */
		0x00, 0x00, 0x00, 0x00, /* x0=0 x1=0 */
		0x00, 0x00, 0x0e, 0x00, /* y0=0 y1=14 */
		0x00, 0x00, 0x00,       /* bkg black */
		0x00, 0x0f,             /* SHORT_VBAR_CACHE_MISS yOn=0 yOff=15 */
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xb6, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xb6, 0x66, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xb6, 0x66, 0xdb, 0x90, 0x3a, 0xff, 0xff, 0xb6,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	};
	rdp_clearcodec_init(&cc); CHECK(rdp_clearcodec_alloc(&cc, NULL) == 0, "clear alloc");
	CHECK(px && px2, "nomem");
	rc = rdp_clearcodec_decode(&cc, NULL, miss_only, sizeof(miss_only), px, 7, 7, 15);
	CHECK(rc == 0, "miss_only rc %d", rc);
	CHECK(cc.st.bands == 1, "bands");
	CHECK(cc.vbar[0].h == 15 && cc.vbar_cursor == 1, "vbar0 h=%u cur=%u", cc.vbar[0].h, cc.vbar_cursor);
	CHECK(px[0] == 0xFFFFFFFFu, "miss (0,0)");
	CHECK(px[10 * 7 + 0] == 0xFF3A90DBu, "miss (0,10) %08x", px[10 * 7]);

	/* Hit the slot we just wrote (index 0) across columns 1..6 */
	{
		static uint8_t hits[64];
		struct rdp_stream s;
		uint8_t* payload;
		size_t band_len;
		rdp_s_init(&s, hits, sizeof(hits));
		rdp_s_u8(&s, 0x00); rdp_s_u8(&s, 0x0c); /* seq 12 */
		rdp_s_u32le(&s, 0); /* residual */
		/* bands length filled later */
		rdp_s_u32le(&s, 0); /* placeholder */
		rdp_s_u32le(&s, 0); /* sub */
		payload = hits + s.pos;
		{
			struct rdp_stream b;
			int col;
			rdp_s_init(&b, payload, sizeof(hits) - s.pos);
			rdp_s_u16le(&b, 1); rdp_s_u16le(&b, 6); /* x 1..6 */
			rdp_s_u16le(&b, 0); rdp_s_u16le(&b, 14);
			rdp_s_u8(&b, 0); rdp_s_u8(&b, 0); rdp_s_u8(&b, 0);
			for (col = 0; col < 6; col++)
				rdp_s_u16le(&b, 0x8000); /* VBAR_CACHE_HIT index 0 */
			band_len = b.pos;
		}
		hits[6] = (uint8_t)(band_len & 0xff);
		hits[7] = (uint8_t)((band_len >> 8) & 0xff);
		hits[8] = hits[9] = 0;
		rc = rdp_clearcodec_decode(&cc, NULL, hits, 14 + band_len, px, 7, 7, 15);
		CHECK(rc == 0, "hits rc %d", rc);
		for (i = 1; i < 7; i++)
			CHECK(px[10 * 7 + i] == 0xFF3A90DBu, "hit col %d", i);
	}

	/* Full MS ex4: seed the hit indices the dump uses, then decode */
	{
		uint8_t seed[15 * 3];
		int k;
		memcpy(seed, miss_only + (sizeof(miss_only) - 45), 45);
		for (k = 0; k < 6; k++)
		{
			uint16_t idx = (uint16_t)(0x1146 + k);
			/* last hit in dump is 0x111b not sequential — handle below */
			CHECK(vbar_store_test(&cc, idx, seed, 15) == 0, "seed %u", idx);
		}
		CHECK(vbar_store_test(&cc, 0x111b, seed, 15) == 0, "seed 0x111b");
		memset(px, 0, 7 * 15 * 4);
		cc.expect_seq = 0x0b; /* match dump */
		rc = rdp_clearcodec_decode(&cc, NULL, ex4, sizeof(ex4), px, 7, 7, 15);
		CHECK(rc == 0, "ex4 rc %d", rc);
		CHECK(cc.st.glyph_store >= 1 && cc.glyph[120].n == 105, "glyph120 n=%u", cc.glyph[120].n);
		CHECK(px[10 * 7 + 3] == 0xFF3A90DBu, "ex4 mid");
	}

	/* Tiny residual glyph store + MS ex1 hit */
	{
		uint8_t store[] = {
			0x01, 0xc2, 0x11, 0x00,
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x11, 0x22, 0x33, 0x01, 0x44, 0x55, 0x66, 0x01
		};
		cc.expect_seq = 0xc2;
		rc = rdp_clearcodec_decode(&cc, NULL, store, sizeof(store), px2, 2, 2, 1);
		CHECK(rc == 0, "tiny glyph store rc %d", rc);
		CHECK(cc.glyph[17].n == 2, "glyph17 n");
		rc = rdp_clearcodec_decode(&cc, NULL, ex1, sizeof(ex1), px2, 2, 2, 1);
		CHECK(rc == 0, "ex1 glyph hit rc %d", rc);
		CHECK(cc.st.glyph_hit >= 1, "glyph hit stat");
		CHECK(px2[0] == 0xFF332211u && px2[1] == 0xFF665544u, "glyph hit pixels %08x %08x",
		      px2[0], px2[1]);
	}

	rdp_clearcodec_free(&cc, NULL);
	free(px);
	free(px2);
}

static void test_raw_subcodec(void)
{
	struct rdp_clearcodec cc;
	uint32_t px[4];
	/* flags=0 seq=0; residual0 bands0 sub= 2+2+2+2+4+1+12 = 25? 
	 * x0=0 y0=0 w=2 h=2 bytecount=12 id=0 + 12 BGR */
	uint8_t stream[] = {
		0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x19, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00,
		0x0c, 0x00, 0x00, 0x00, 0x00,
		0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c
	};
	int rc;
	rdp_clearcodec_init(&cc); CHECK(rdp_clearcodec_alloc(&cc, NULL) == 0, "clear alloc");
	rc = rdp_clearcodec_decode(&cc, NULL, stream, sizeof(stream), px, 2, 2, 2);
	CHECK(rc == 0, "raw rc %d", rc);
	CHECK(px[0] == 0xFF030201u && px[3] == 0xFF0C0B0Au, "raw pixels");
	CHECK(cc.st.sub_raw == 1, "raw stat");
	rdp_clearcodec_free(&cc, NULL);
}

static size_t pdu(uint8_t* out, uint16_t cmd, const uint8_t* body, size_t blen)
{
	struct rdp_stream s;
	rdp_s_init(&s, out, 8 + blen + 8);
	rdp_s_u16le(&s, cmd);
	rdp_s_u16le(&s, 0);
	rdp_s_u32le(&s, (uint32_t)(8 + blen));
	if (blen)
		memcpy(out + 8, body, blen);
	return 8 + blen;
}

static void test_gfx_wire_clear(void)
{
	struct rdp_gfx g;
	uint8_t in[512], body[256];
	struct rdp_stream s;
	size_t n = 0, blen;
	int rc;
	CHECK(rdp_gfx_init(&g, NULL, 64, 64, NULL, NULL) == 0, "gfx init");
	rdp_s_init(&s, body, sizeof(body));
	rdp_s_u16le(&s, 1); rdp_s_u16le(&s, 64); rdp_s_u16le(&s, 24); rdp_s_u8(&s, 0x20);
	n += pdu(in + n, RDPGFX_CMDID_CREATESURFACE, body, s.pos);
	rdp_s_init(&s, body, sizeof(body));
	rdp_s_u16le(&s, 1); rdp_s_u16le(&s, RDPGFX_CODECID_CLEARCODEC); rdp_s_u8(&s, 0x20);
	rdp_s_u16le(&s, 0); rdp_s_u16le(&s, 0); rdp_s_u16le(&s, 64); rdp_s_u16le(&s, 24);
	rdp_s_u32le(&s, (uint32_t)sizeof(ex3_hdr_residual));
	memcpy(body + s.pos, ex3_hdr_residual, sizeof(ex3_hdr_residual));
	blen = s.pos + sizeof(ex3_hdr_residual);
	n += pdu(in + n, RDPGFX_CMDID_WIRETOSURFACE_1, body, blen);
	rc = rdp_gfx_process_pdus(&g, in, n);
	CHECK(rc == 0, "gfx pdus rc %d err=%u", rc, g.st.errors);
	CHECK(g.st.clearcodec == 1, "gfx clearcodec count %u", g.st.clearcodec);
	CHECK(g.surf[0].used && g.surf[0].px[0] == 0xFFFEFEFEu, "gfx surf px0");
	rdp_gfx_free(&g);
}

int main(void)
{
	test_residual_fill();
	test_rlex_ex2();
	test_glyph_and_vbar_ex4();
	test_raw_subcodec();
	test_gfx_wire_clear();
	printf("clearcodec: %d passed, %d failed\n", passes, fails);
	return fails ? 1 : 0;
}
