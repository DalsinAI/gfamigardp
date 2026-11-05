/*
 * GFAmigaRDP host tests: the Graphics Pipeline scaffold ([MS-RDPEGFX]
 * 2.2.2, 2.2.3, 2.2.5, 3.1.9.1, 3.3.5) and Planar ([MS-RDPEGDI] 2.2.2.5.1,
 * 3.1.9). The ZGFX and RLE vectors are the specs' own worked examples;
 * the rest are synthetic PDUs built from the public layouts (CLEAN-ROOM.md).
 *   cc -I../../src/core -I../../src/codec -o test_gfx test_gfx.c \
 *      ../../src/core/rdpgfx.c ../../src/codec/gfx_fb.c \
 *      ../../src/codec/zgfx.c ../../src/codec/planar.c
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rdpgfx.h"
#include "planar.h"
#include "zgfx.h"
#include "gfx_fb.h"
#include "stream.h"

static int fails, passes;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint8_t sent[8][512];
static size_t sent_len[8];
static int nsent;

static int capture_send(void* user, const uint8_t* p, size_t n)
{
	(void)user;
	if (nsent >= 8 || n > sizeof(sent[0]))
		return -1;
	memcpy(sent[nsent], p, n);
	sent_len[nsent++] = n;
	return 0;
}

static uint32_t le32(const uint8_t* p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* "0 0100..." -> bytes, plus the unused-bits trailer of RDP8 bulk data */
static size_t pack_bits(const char* bits, uint8_t* out)
{
	size_t n = 0, i;
	memset(out, 0, 64);
	for (i = 0; *bits; bits++)
	{
		if (*bits == ' ')
			continue;
		if (*bits == '1')
			out[i / 8] |= (uint8_t)(0x80 >> (i % 8));
		i++;
	}
	n = (i + 7) / 8;
	out[n] = (uint8_t)(n * 8 - i);
	return n + 1;
}

static void test_zgfx(void)
{
	struct rdp_zgfx z;
	const uint8_t* out;
	size_t n, i;
	uint8_t buf[80];
	int ok;
	static const uint8_t abc[] = { 0xE0, 0x24, 0x20, 0x90, 0x88, 0x71, 0x1F, 0xB2, 0x01 };
	static const uint8_t raw[] = { 0xE0, 0x04, 'h', 'i' };

	CHECK(rdp_zgfx_init(&z, NULL) == 0, "zgfx init");
	/* 3.1.9.1.2 example: a literal 0x49, then a match of distance 1, length 9 */
	buf[0] = 0xE0;
	buf[1] = 0x24;
	n = pack_bits("0 01001001  10001 00001  110 001", buf + 2) + 2;
	CHECK(rdp_zgfx_decompress(&z, buf, n, &out, &n) == 0 && n == 10, "zgfx 0x49 x10: len %u", (unsigned)n);
	for (i = 0, ok = 1; i < 10 && n == 10; i++)
		ok &= out[i] == 0x49;
	CHECK(ok, "zgfx 0x49 bytes");

	CHECK(rdp_zgfx_decompress(&z, abc, sizeof(abc), &out, &n) == 0 && n == 60, "zgfx ABC: len %u", (unsigned)n);
	for (i = 0, ok = 1; i < 60 && n == 60; i++)
		ok &= out[i] == "ABC"[i % 3];
	CHECK(ok, "zgfx ABC x20");

	CHECK(rdp_zgfx_decompress(&z, raw, sizeof(raw), &out, &n) == 0 && n == 2 && out[0] == 'h', "zgfx raw");
	/* the history keeps "hi" (and the earlier output): match distance 2, length 3 -> "hih" */
	buf[0] = 0xE0;
	buf[1] = 0x24;
	n = pack_bits("10001 00010  0", buf + 2) + 2;
	CHECK(rdp_zgfx_decompress(&z, buf, n, &out, &n) == 0 && n == 3 && !memcmp(out, "hih", 3), "zgfx history");

	/* unencoded run: distance token 0, 15-bit count 2, byte align, raw bytes */
	buf[0] = 0xE0;
	buf[1] = 0x24;
	n = pack_bits("10001 00000 000000000000010 0000000 01011000 01011001", buf + 2) + 2;
	CHECK(rdp_zgfx_decompress(&z, buf, n, &out, &n) == 0 && n == 2 && !memcmp(out, "XY", 2), "zgfx unencoded run");

	/* multipart: two raw segments */
	{
		static const uint8_t mp[] = { 0xE1, 2, 0, 5, 0, 0, 0, 3, 0, 0, 0, 0x04, 'a', 'b', 4, 0, 0, 0, 0x04, 'c', 'd', 'e' };
		CHECK(rdp_zgfx_decompress(&z, mp, sizeof(mp), &out, &n) == 0 && n == 5 && !memcmp(out, "abcde", 5), "zgfx multipart");
		CHECK(rdp_zgfx_decompress(&z, mp, sizeof(mp) - 1, &out, &n) < 0, "zgfx multipart short");
	}
	/* a match reaching beyond the history it has is still in the (zeroed) ring;
	 * a bad descriptor and a non-RDP8 type are errors */
	buf[0] = 0xE2;
	CHECK(rdp_zgfx_decompress(&z, buf, 4, &out, &n) < 0, "zgfx bad descriptor");
	buf[0] = 0xE0;
	buf[1] = 0x03;
	CHECK(rdp_zgfx_decompress(&z, buf, 3, &out, &n) < 0, "zgfx bad type");
	rdp_zgfx_free(&z);
}

static void test_planar(void)
{
	/* [MS-RDPEGDI] 3.1.9.2 examples */
	static const uint8_t v1[] = { 0x13, 0xFF, 0x20, 0xFE, 0xFD, 0x60, 0x01, 0x7D, 0xF5, 0xC2, 0x9A,
	                              0x38, 0x60, 0x01, 0x67, 0x8B, 0xA3, 0x78, 0xAF };
	static const uint8_t e1[] = { 255, 255, 255, 255, 254, 253, 254, 192, 132, 96, 75, 25,
	                              253, 140, 62, 14, 135, 193 };
	static const uint8_t v2[] = { 0x1F, 0x41, 0xF2, 0x52 };
	uint8_t plane[128], scratch[256];
	uint32_t px[16];
	long n;
	int i, ok;

	n = rdp_planar_rle_plane(v1, sizeof(v1), 6, 3, plane);
	CHECK(n == (long)sizeof(v1) && !memcmp(plane, e1, 18), "planar RLE example 1 (%ld)", n);
	n = rdp_planar_rle_plane(v2, sizeof(v2), 100, 1, plane);
	for (i = 0, ok = 1; i < 100; i++)
		ok &= plane[i] == 'A';
	CHECK(n == 4 && ok, "planar RLE example 2");
	CHECK(rdp_planar_rle_plane(v2, 3, 100, 1, plane) < 0, "planar RLE short");
	CHECK(rdp_planar_rle_plane(v2, sizeof(v2), 99, 1, plane) < 0, "planar RLE overrun");

	/* raw ARGB, no alpha plane, 2x2, one pad byte */
	{
		static const uint8_t s[] = { 0x20, 1, 2, 3, 4, 10, 20, 30, 40, 100, 101, 102, 103, 0 };
		CHECK(rdp_planar_decode(s, sizeof(s), 2, 2, px, 2, scratch, 0) == 0, "planar raw");
		CHECK(px[0] == 0xFF010A64u && px[3] == 0xFF042867u, "planar raw px %08lx %08lx", (unsigned long)px[0], (unsigned long)px[3]);
	}
	/* RLE with alpha, 2x1: every plane one segment of 2 raw bytes */
	{
		static const uint8_t s[] = { 0x10, 0x20, 0x80, 0x7F, 0x20, 1, 2, 0x20, 3, 4, 0x20, 5, 6 };
		CHECK(rdp_planar_decode(s, sizeof(s), 2, 1, px, 2, scratch, 0) == 0, "planar RLE+alpha");
		CHECK(px[0] == 0x80010305u && px[1] == 0x7F020406u, "planar RLE+alpha px");
	}
	/* AYCoCg, CLL 1, raw, NA, 1x1: Y 100, Co 20 stored as 10, Cg 0 -> R 110 G 100 B 90 */
	{
		static const uint8_t s[] = { 0x21, 100, 10, 0, 0 };
		CHECK(rdp_planar_decode(s, sizeof(s), 1, 1, px, 1, scratch, 0) == 0 && px[0] == 0xFF6E645Au,
		      "planar YCoCg %08lx", (unsigned long)px[0]);
		CHECK(rdp_planar_decode(s, sizeof(s), 1, 1, px, 1, scratch, RDP_PLANAR_SWAP_RB) == 0 && px[0] == 0xFF5A646Eu,
		      "planar YCoCg swap");
	}
	/* AYCoCg with chroma super-sampling, 2x2: one chroma sample for all four */
	{
		static const uint8_t s[] = { 0x29, 50, 60, 70, 80, 0xF6, 0 };   /* Co -20 (stored -10), Cg 0 */
		CHECK(rdp_planar_decode(s, sizeof(s), 2, 2, px, 2, scratch, 0) == 0 && px[0] == 0xFF28323Cu,
		      "planar CS %08lx", (unsigned long)px[0]);
		CHECK(px[3] == 0xFF46505Au, "planar CS px3 %08lx", (unsigned long)px[3]);
	}
	CHECK(rdp_planar_decode((const uint8_t*)"\x08", 1, 1, 1, px, 1, scratch, 0) < 0, "planar CS without CLL");

	/* uncompressed: B,G,R,X bytes */
	{
		static const uint8_t s[] = { 1, 2, 3, 9, 4, 5, 6, 9 };
		CHECK(rdp_uncompressed_decode(s, 8, 2, 1, px, 2, 0) == 0 && px[0] == 0xFF030201u && px[1] == 0xFF060504u,
		      "uncompressed");
		CHECK(rdp_uncompressed_decode(s, 8, 2, 1, px, 2, 1) == 0 && px[0] == 0x09030201u, "uncompressed alpha");
		CHECK(rdp_uncompressed_decode(s, 7, 2, 1, px, 2, 0) < 0, "uncompressed short");
	}
}

static void test_dirty(void)
{
	struct rdp_dirty d;
	struct rdp_rect r, b;
	int i;
	rdp_dirty_clear(&d);
	r.x0 = 10; r.y0 = 10; r.x1 = 20; r.y1 = 20;
	rdp_dirty_add(&d, &r, 100, 100);
	r.x0 = 20; r.x1 = 30;   /* touching: merges */
	rdp_dirty_add(&d, &r, 100, 100);
	CHECK(d.n == 1 && d.r[0].x0 == 10 && d.r[0].x1 == 30, "dirty merge");
	r.x0 = 90; r.y0 = 90; r.x1 = 200; r.y1 = 200;   /* clipped */
	rdp_dirty_add(&d, &r, 100, 100);
	CHECK(d.n == 2 && d.r[1].x1 == 100 && d.r[1].y1 == 100, "dirty clip");
	r.x0 = 5; r.x1 = 5;
	rdp_dirty_add(&d, &r, 100, 100);
	CHECK(d.n == 2, "dirty empty dropped");
	for (i = 0; i < 40; i++)
	{
		r.x0 = (uint16_t)(i * 2 + 40); r.x1 = (uint16_t)(r.x0 + 1);
		r.y0 = 50; r.y1 = 51;
		rdp_dirty_add(&d, &r, 200, 200);
	}
	CHECK(d.overflow && d.n <= RDP_DIRTY_MAX, "dirty overflow folds (%u)", d.n);
	CHECK(rdp_dirty_bounds(&d, &b) && b.x0 == 10 && b.y0 == 10 && b.x1 == 119 && b.y1 == 100, "dirty bounds");
}

static size_t pdu(uint8_t* buf, uint16_t cmd, const uint8_t* body, size_t n)
{
	struct rdp_stream s;
	rdp_s_init(&s, buf, 8 + n);
	rdp_s_u16le(&s, cmd);
	rdp_s_u16le(&s, 0);
	rdp_s_u32le(&s, (uint32_t)(8 + n));
	rdp_s_bytes(&s, body, n);
	return s.pos;
}

static void test_caps(void)
{
	uint8_t buf[256];
	size_t n = rdp_gfx_build_caps(buf, sizeof(buf)), pos = 10;
	uint16_t count = (uint16_t)(buf[8] | (buf[9] << 8)), i;
	int avc_ok = 1, has107 = 0, has101 = 0;
	CHECK(n > 10 && buf[0] == 0x12 && le32(buf + 4) == n, "caps header");
	for (i = 0; i < count && pos + 12 <= n; i++, pos += 12)
	{
		uint32_t v = le32(buf + pos), len = le32(buf + pos + 4), fl = le32(buf + pos + 8);
		if (len != 4)
			avc_ok = 0;
		if (fl & (RDPGFX_CAPS_FLAG_AVC420_ENABLED | RDPGFX_CAPS_FLAG_AVC_THINCLIENT))
			avc_ok = 0;
		if ((v >> 16) == 0x000A && !(fl & RDPGFX_CAPS_FLAG_AVC_DISABLED))
			avc_ok = 0;
		has107 |= v == RDPGFX_CAPVERSION_107;
		has101 |= v == RDPGFX_CAPVERSION_101;
	}
	CHECK(pos == n && count == 9, "caps count %u", count);
	CHECK(avc_ok && has107 && !has101, "caps never offer AVC");
	CHECK(rdp_gfx_build_caps(buf, 20) == 0, "caps small buffer");
}

static void test_flow(void)
{
	struct rdp_gfx g;
	uint8_t in[2048], body[512];
	size_t n = 0;
	struct rdp_stream s;
	struct rdp_dirty d;
	uint32_t frames;
	int rc;

	nsent = 0;
	CHECK(rdp_gfx_init(&g, NULL, 64, 48, capture_send, NULL) == 0, "gfx init");
	CHECK(rdp_gfx_on_open(&g) == 0 && nsent == 1 && sent[0][0] == 0x12, "caps advertise sent");

	/* CapsConfirm 10.7 */
	rdp_s_init(&s, body, sizeof(body));
	rdp_s_u32le(&s, RDPGFX_CAPVERSION_107); rdp_s_u32le(&s, 4); rdp_s_u32le(&s, RDPGFX_CAPS_FLAG_AVC_DISABLED);
	n += pdu(in + n, RDPGFX_CMDID_CAPSCONFIRM, body, s.pos);
	/* ResetGraphics 64x64, one monitor, padded to 340 */
	memset(body, 0, sizeof(body));
	rdp_s_init(&s, body, sizeof(body));
	rdp_s_u32le(&s, 64); rdp_s_u32le(&s, 64); rdp_s_u32le(&s, 1);
	n += pdu(in + n, RDPGFX_CMDID_RESETGRAPHICS, body, 332);
	/* CreateSurface 1, 32x32 XRGB; Map it at (8,8) */
	rdp_s_init(&s, body, sizeof(body));
	rdp_s_u16le(&s, 1); rdp_s_u16le(&s, 32); rdp_s_u16le(&s, 32); rdp_s_u8(&s, 0x20);
	n += pdu(in + n, RDPGFX_CMDID_CREATESURFACE, body, s.pos);
	rdp_s_init(&s, body, sizeof(body));
	rdp_s_u16le(&s, 1); rdp_s_u16le(&s, 0); rdp_s_u32le(&s, 8); rdp_s_u32le(&s, 8);
	n += pdu(in + n, RDPGFX_CMDID_MAPSURFACETOOUTPUT, body, s.pos);
	/* StartFrame 7 */
	rdp_s_init(&s, body, sizeof(body));
	rdp_s_u32le(&s, 0); rdp_s_u32le(&s, 7);
	n += pdu(in + n, RDPGFX_CMDID_STARTFRAME, body, s.pos);
	/* SolidFill red over (0,0)-(16,16) */
	rdp_s_init(&s, body, sizeof(body));
	rdp_s_u16le(&s, 1); rdp_s_u8(&s, 0); rdp_s_u8(&s, 0); rdp_s_u8(&s, 0xFF); rdp_s_u8(&s, 0);
	rdp_s_u16le(&s, 1); rdp_s_u16le(&s, 0); rdp_s_u16le(&s, 0); rdp_s_u16le(&s, 16); rdp_s_u16le(&s, 16);
	n += pdu(in + n, RDPGFX_CMDID_SOLIDFILL, body, s.pos);
	/* SurfaceToSurface (0,0)-(4,4) to (20,20) and (30,30) (clipped to 2x2) */
	rdp_s_init(&s, body, sizeof(body));
	rdp_s_u16le(&s, 1); rdp_s_u16le(&s, 1);
	rdp_s_u16le(&s, 0); rdp_s_u16le(&s, 0); rdp_s_u16le(&s, 4); rdp_s_u16le(&s, 4);
	rdp_s_u16le(&s, 2); rdp_s_u16le(&s, 20); rdp_s_u16le(&s, 20); rdp_s_u16le(&s, 30); rdp_s_u16le(&s, 30);
	n += pdu(in + n, RDPGFX_CMDID_SURFACETOSURFACE, body, s.pos);
	/* WireToSurface1 uncompressed 2x1 at (16,0): blue, green */
	rdp_s_init(&s, body, sizeof(body));
	rdp_s_u16le(&s, 1); rdp_s_u16le(&s, RDPGFX_CODECID_UNCOMPRESSED); rdp_s_u8(&s, 0x20);
	rdp_s_u16le(&s, 16); rdp_s_u16le(&s, 0); rdp_s_u16le(&s, 18); rdp_s_u16le(&s, 1);
	rdp_s_u32le(&s, 8);
	rdp_s_u8(&s, 0xFF); rdp_s_u8(&s, 0); rdp_s_u8(&s, 0); rdp_s_u8(&s, 0);
	rdp_s_u8(&s, 0); rdp_s_u8(&s, 0xFF); rdp_s_u8(&s, 0); rdp_s_u8(&s, 0);
	n += pdu(in + n, RDPGFX_CMDID_WIRETOSURFACE_1, body, s.pos);
	/* WireToSurface1 AVC420 (never offered): skipped, counted */
	rdp_s_init(&s, body, sizeof(body));
	rdp_s_u16le(&s, 1); rdp_s_u16le(&s, RDPGFX_CODECID_AVC420); rdp_s_u8(&s, 0x20);
	rdp_s_u16le(&s, 0); rdp_s_u16le(&s, 0); rdp_s_u16le(&s, 16); rdp_s_u16le(&s, 16);
	rdp_s_u32le(&s, 2); rdp_s_u8(&s, 0); rdp_s_u8(&s, 0);
	n += pdu(in + n, RDPGFX_CMDID_WIRETOSURFACE_1, body, s.pos);
	/* SurfaceToCache slot 1 = (0,0)-(2,2); CacheToSurface slot 1 at (0,30) */
	rdp_s_init(&s, body, sizeof(body));
	rdp_s_u16le(&s, 1); rdp_s_u32le(&s, 0); rdp_s_u32le(&s, 0); rdp_s_u16le(&s, 1);
	rdp_s_u16le(&s, 0); rdp_s_u16le(&s, 0); rdp_s_u16le(&s, 2); rdp_s_u16le(&s, 2);
	n += pdu(in + n, RDPGFX_CMDID_SURFACETOCACHE, body, s.pos);
	rdp_s_init(&s, body, sizeof(body));
	rdp_s_u16le(&s, 1); rdp_s_u16le(&s, 1); rdp_s_u16le(&s, 1); rdp_s_u16le(&s, 0); rdp_s_u16le(&s, 30);
	n += pdu(in + n, RDPGFX_CMDID_CACHETOSURFACE, body, s.pos);
	/* EndFrame 7 */
	rdp_s_init(&s, body, sizeof(body));
	rdp_s_u32le(&s, 7);
	n += pdu(in + n, RDPGFX_CMDID_ENDFRAME, body, s.pos);

	/* through the real input path: one uncompressed RDP8 segment */
	{
		static uint8_t seg[2100];
		seg[0] = 0xE0;
		seg[1] = 0x04;
		memcpy(seg + 2, in, n);
		rc = rdp_gfx_input(&g, seg, n + 2);
	}
	CHECK(rc == 0, "gfx flow rc %d (errors %lu)", rc, (unsigned long)g.st.errors);
	CHECK(g.caps_ok && g.caps_version == RDPGFX_CAPVERSION_107, "caps confirm");
	CHECK(g.fb.w == 64 && g.fb.h == 64, "reset graphics resized");
	CHECK(g.fb.px[8 * 64 + 8] == 0xFFFF0000u, "fill reached output (%08lx)", (unsigned long)g.fb.px[8 * 64 + 8]);
	CHECK(g.fb.px[(8 + 21) * 64 + 8 + 21] == 0xFFFF0000u, "s2s copy");
	CHECK(g.fb.px[(8 + 31) * 64 + 8 + 31] == 0xFFFF0000u && g.surf[0].px[31 * 32 + 31] == 0xFFFF0000u, "s2s clipped");
	CHECK(g.fb.px[8 * 64 + 8 + 16] == 0xFF0000FFu && g.fb.px[8 * 64 + 8 + 17] == 0xFF00FF00u, "uncompressed tile");
	CHECK(g.fb.px[(8 + 31) * 64 + 8 + 1] == 0xFFFF0000u, "cache to surface");
	CHECK(g.st.avc_seen == 1 && g.st.uncompressed == 1 && g.st.solid_fills == 1, "stats");
	CHECK(nsent == 2 && sent_len[1] == 20 && sent[1][0] == 0x0D && le32(sent[1] + 12) == 7 && le32(sent[1] + 16) == 1,
	      "frame ack (%d sent)", nsent);
	CHECK(rdp_fb_take_dirty(&g.fb, &d, &frames) >= 1 && frames == 1, "dirty for the UI");
	CHECK(d.r[0].x0 == 0 && d.r[0].x1 == 64, "dirty covers reset");
	CHECK(rdp_fb_take_dirty(&g.fb, &d, NULL) == 0, "dirty taken");

	/* bad: a CapsConfirm we did not offer, a fill on a missing surface, a short pdu */
	rdp_s_init(&s, body, sizeof(body));
	rdp_s_u32le(&s, RDPGFX_CAPVERSION_101); rdp_s_u32le(&s, 4); rdp_s_u32le(&s, 0);
	n = pdu(in, RDPGFX_CMDID_CAPSCONFIRM, body, s.pos);
	CHECK(rdp_gfx_process_pdus(&g, in, n) < 0, "caps confirm not offered");
	rdp_s_init(&s, body, sizeof(body));
	rdp_s_u16le(&s, 9); rdp_s_u32le(&s, 0); rdp_s_u16le(&s, 0);
	n = pdu(in, RDPGFX_CMDID_SOLIDFILL, body, s.pos);
	CHECK(rdp_gfx_process_pdus(&g, in, n) < 0, "fill on missing surface");
	CHECK(rdp_gfx_process_pdus(&g, in, n - 1) < 0, "short pdu");
	CHECK(rdp_gfx_input(&g, (const uint8_t*)"\xE7", 1) < 0, "bad segment");
	rdp_gfx_free(&g);
}

int main(void)
{
	test_zgfx();
	test_planar();
	test_dirty();
	test_caps();
	test_flow();
	printf("%d passed, %d failed\n", passes, fails);
	return fails ? 1 : 0;
}
