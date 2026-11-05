/*
 * GFAmigaRDP host tests: CLIPRDR text path ([MS-RDPECLIP]).
 *   cc -I../../src/core -o test_cliprdr test_cliprdr.c \
 *      ../../src/core/vchan.c ../../src/core/utf16.c ../../src/core/cliprdr.c
 * Synthetic PDUs from the public [MS-RDPECLIP] layouts only (CLEAN-ROOM.md).
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vchan.h"
#include "cliprdr.h"
#include "utf16.h"

static int fails, passes;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint8_t sent[32][4096];
static size_t sent_len[32];
static int nsent;

static int capture_send(void* user, const uint8_t* pdu, size_t len)
{
	(void)user;
	if (nsent >= 32 || len > sizeof(sent[0]))
		return -1;
	memcpy(sent[nsent], pdu, len);
	sent_len[nsent] = len;
	nsent++;
	return 0;
}

static char got_text[1024];
static int got_n;

static void on_text(void* user, const char* utf8)
{
	(void)user;
	got_n++;
	strncpy(got_text, utf8 ? utf8 : "", sizeof(got_text) - 1);
}

static void wu16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wu32(uint8_t* p, uint32_t v)
{
	p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* CHANNEL_PDU_HEADER + payload as one MCS chunk (FIRST|LAST). */
static int feed_clip(struct rdp_cliprdr* c, const uint8_t* pdu, size_t len)
{
	uint8_t wrap[8 + 4096];
	int n = rdp_vc_build(wrap, sizeof(wrap), pdu, len, RDP_VC_FLAG_FIRST | RDP_VC_FLAG_LAST);
	if (n < 0)
		return n;
	wrap[0] = (uint8_t)len;
	wrap[1] = (uint8_t)(len >> 8);
	wrap[2] = (uint8_t)(len >> 16);
	wrap[3] = (uint8_t)(len >> 24);
	return rdp_cliprdr_input(c, wrap, (size_t)n);
}

static void reset_cap(void)
{
	nsent = 0;
	got_n = 0;
	got_text[0] = 0;
	memset(sent, 0, sizeof(sent));
}

static void test_build_helpers(void)
{
	uint8_t buf[256];
	int n;
	char utf8[64];
	uint8_t u16[32];

	n = rdp_cliprdr_build_header(buf, sizeof(buf), RDP_CB_MONITOR_READY, 0, 0);
	CHECK(n == 8 && buf[0] == 1 && buf[1] == 0, "header type");
	n = rdp_cliprdr_build_caps(buf, sizeof(buf), RDP_CB_USE_LONG_FORMAT_NAMES);
	CHECK(n > 8 && buf[0] == 7, "caps type");
	n = rdp_cliprdr_build_format_list_text(buf, sizeof(buf), 1);
	CHECK(n > 8 && buf[0] == 2, "format list long");
	n = rdp_cliprdr_build_format_list_text(buf, sizeof(buf), 0);
	CHECK(n > 8, "format list short");

	/* "Hi" as UTF-16LE + NUL */
	u16[0] = 'H'; u16[1] = 0; u16[2] = 'i'; u16[3] = 0; u16[4] = 0; u16[5] = 0;
	CHECK(rdp_cliprdr_utf16le_to_utf8(utf8, sizeof(utf8), u16, 6) == 2 && !strcmp(utf8, "Hi"), "u16→utf8");
	n = rdp_cliprdr_utf8_to_utf16le_z(u16, sizeof(u16), "Hi");
	CHECK(n == 6 && u16[0] == 'H' && u16[2] == 'i' && u16[4] == 0, "utf8→u16z");
}

static void test_init_handshake(void)
{
	struct rdp_cliprdr c;
	uint8_t pdu[64];
	int i, saw_caps = 0, saw_list = 0;

	reset_cap();
	rdp_cliprdr_init(&c, capture_send, on_text, NULL);
	/* Caps from server (General, long names) */
	{
		uint8_t body[16];
		wu16(body, 1);      /* cCapabilitiesSets */
		wu16(body + 2, 0);  /* pad */
		wu16(body + 4, RDP_CB_CAPSTYPE_GENERAL);
		wu16(body + 6, 12); /* length */
		wu32(body + 8, RDP_CB_CAPS_VERSION_2);
		wu32(body + 12, RDP_CB_USE_LONG_FORMAT_NAMES);
		rdp_cliprdr_build_header(pdu, sizeof(pdu), RDP_CB_CLIP_CAPS, 0, 16);
		memcpy(pdu + 8, body, 16);
		CHECK(feed_clip(&c, pdu, 24) >= 0, "feed caps");
	}
	/* Monitor Ready → our Caps + Format List */
	rdp_cliprdr_build_header(pdu, sizeof(pdu), RDP_CB_MONITOR_READY, 0, 0);
	CHECK(feed_clip(&c, pdu, 8) >= 0, "feed monitor ready");
	CHECK(c.inited == 1, "inited");
	for (i = 0; i < nsent; i++)
	{
		uint16_t t = (uint16_t)(sent[i][0] | (sent[i][1] << 8));
		if (t == RDP_CB_CLIP_CAPS) saw_caps = 1;
		if (t == RDP_CB_FORMAT_LIST) saw_list = 1;
	}
	CHECK(saw_caps && saw_list, "client Caps+Format List after Monitor Ready (got %d pdus)", nsent);
	rdp_cliprdr_free(&c);
}

static void test_remote_to_local(void)
{
	struct rdp_cliprdr c;
	uint8_t pdu[128];
	uint8_t u16[16];
	int i, saw_req = 0, saw_ok = 0;

	reset_cap();
	rdp_cliprdr_init(&c, capture_send, on_text, NULL);
	c.inited = 1;
	c.long_names = 1;
	c.our_flags = RDP_CB_USE_LONG_FORMAT_NAMES;
	c.peer_flags = RDP_CB_USE_LONG_FORMAT_NAMES;

	/* Peer Format List: CF_UNICODETEXT long name */
	{
		uint8_t body[32];
		size_t n = 0;
		wu32(body + n, RDP_CF_UNICODETEXT); n += 4;
		/* wszFormatName "\0" */
		body[n++] = 0; body[n++] = 0;
		rdp_cliprdr_build_header(pdu, sizeof(pdu), RDP_CB_FORMAT_LIST, 0, (uint32_t)n);
		memcpy(pdu + 8, body, n);
		nsent = 0;
		CHECK(feed_clip(&c, pdu, 8 + n) >= 0, "feed format list");
	}
	for (i = 0; i < nsent; i++)
	{
		uint16_t t = (uint16_t)(sent[i][0] | (sent[i][1] << 8));
		if (t == RDP_CB_FORMAT_LIST_RESPONSE) saw_ok = 1;
		if (t == RDP_CB_FORMAT_DATA_REQUEST)
		{
			saw_req = 1;
			CHECK((sent[i][8] | (sent[i][9] << 8) | (sent[i][10] << 16) | (sent[i][11] << 24))
			      == (int)RDP_CF_UNICODETEXT, "requested UNICODETEXT");
		}
	}
	CHECK(saw_ok && saw_req, "Format List Response + Data Request");

	/* Format Data Response: "Hi" UTF-16LE */
	u16[0] = 'H'; u16[1] = 0; u16[2] = 'i'; u16[3] = 0; u16[4] = 0; u16[5] = 0;
	rdp_cliprdr_build_header(pdu, sizeof(pdu), RDP_CB_FORMAT_DATA_RESPONSE, RDP_CB_RESPONSE_OK, 6);
	memcpy(pdu + 8, u16, 6);
	c.pending_req = RDP_CF_UNICODETEXT;
	CHECK(feed_clip(&c, pdu, 14) >= 0, "feed data response");
	CHECK(got_n == 1 && !strcmp(got_text, "Hi"), "on_text Hi (got '%s')", got_text);
	CHECK(c.ignore_echo == 1, "ignore_echo set");
	CHECK(rdp_cliprdr_set_local_text(&c, "Hi") == 0 && c.ignore_echo == 0, "echo skipped");
	rdp_cliprdr_free(&c);
}

static void test_local_to_remote(void)
{
	struct rdp_cliprdr c;
	uint8_t pdu[64];
	int i, saw_list = 0, saw_data = 0;

	reset_cap();
	rdp_cliprdr_init(&c, capture_send, on_text, NULL);
	c.inited = 1;
	c.long_names = 1;
	CHECK(rdp_cliprdr_set_local_text(&c, "Amiga") == 0, "set local");
	for (i = 0; i < nsent; i++)
	{
		uint16_t t = (uint16_t)(sent[i][0] | (sent[i][1] << 8));
		if (t == RDP_CB_FORMAT_LIST) saw_list = 1;
	}
	CHECK(saw_list && c.we_own, "announced Format List");

	/* Peer requests CF_UNICODETEXT */
	nsent = 0;
	rdp_cliprdr_build_header(pdu, sizeof(pdu), RDP_CB_FORMAT_DATA_REQUEST, 0, 4);
	wu32(pdu + 8, RDP_CF_UNICODETEXT);
	CHECK(feed_clip(&c, pdu, 12) >= 0, "data request");
	for (i = 0; i < nsent; i++)
	{
		uint16_t t = (uint16_t)(sent[i][0] | (sent[i][1] << 8));
		uint16_t fl = (uint16_t)(sent[i][2] | (sent[i][3] << 8));
		if (t == RDP_CB_FORMAT_DATA_RESPONSE && (fl & RDP_CB_RESPONSE_OK))
		{
			saw_data = 1;
			/* payload starts with 'A' 0 'm' 0 ... */
			CHECK(sent[i][8] == 'A' && sent[i][10] == 'm', "UTF-16LE Amiga");
		}
	}
	CHECK(saw_data, "served Format Data Response");

	/* Refuse CF_HDROP-like id */
	nsent = 0;
	rdp_cliprdr_build_header(pdu, sizeof(pdu), RDP_CB_FORMAT_DATA_REQUEST, 0, 4);
	wu32(pdu + 8, 15); /* CF_HDROP */
	CHECK(feed_clip(&c, pdu, 12) >= 0, "refuse hdrop");
	CHECK(nsent == 1 && (sent[0][2] | (sent[0][3] << 8)) == RDP_CB_RESPONSE_FAIL, "FAIL for non-text");
	rdp_cliprdr_free(&c);
}

static void test_cf_text(void)
{
	struct rdp_cliprdr c;
	uint8_t pdu[64];
	const char* latin = "xy";

	reset_cap();
	rdp_cliprdr_init(&c, capture_send, on_text, NULL);
	c.inited = 1;
	c.pending_req = RDP_CF_TEXT;
	rdp_cliprdr_build_header(pdu, sizeof(pdu), RDP_CB_FORMAT_DATA_RESPONSE, RDP_CB_RESPONSE_OK, 3);
	memcpy(pdu + 8, latin, 2);
	pdu[10] = 0;
	CHECK(feed_clip(&c, pdu, 11) >= 0, "cf_text feed");
	CHECK(got_n == 1 && !strcmp(got_text, "xy"), "cf_text decode");
	rdp_cliprdr_free(&c);
}

int main(void)
{
	test_build_helpers();
	test_init_handshake();
	test_remote_to_local();
	test_local_to_remote();
	test_cf_text();
	printf("cliprdr: %d passed, %d failed\n", passes, fails);
	return fails ? 1 : 0;
}
