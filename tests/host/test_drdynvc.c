/*
 * GFAmigaRDP host tests: vchan framing and the DYNVC manager ([MS-RDPEDYC]).
 *   cc -I../../src/core -o test_drdynvc test_drdynvc.c \
 *      ../../src/core/vchan.c ../../src/core/drdynvc.c
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vchan.h"
#include "drdynvc.h"

static int fails, passes;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint8_t sent[8][2048];
static size_t sent_len[8];
static int nsent;

static int capture_send(void* user, const uint8_t* pdu, size_t len)
{
	(void)user;
	if (nsent >= 8 || len > sizeof(sent[0]))
		return -1;
	memcpy(sent[nsent], pdu, len);
	sent_len[nsent] = len;
	nsent++;
	return 0;
}

static int got_create;
static uint32_t got_id;
static char got_name[64];
static uint8_t got_data[256];
static size_t got_data_len;
static int got_close;

static int on_create(struct rdp_dynvc* d, uint32_t id, const char* name, void* user)
{
	(void)d; (void)user;
	got_create = 1;
	got_id = id;
	strncpy(got_name, name, sizeof(got_name) - 1);
	return 0;
}
static void on_data(struct rdp_dynvc* d, uint32_t id, const uint8_t* data, size_t len, void* user)
{
	(void)d; (void)id; (void)user;
	if (len > sizeof(got_data)) len = sizeof(got_data);
	memcpy(got_data, data, len);
	got_data_len = len;
}
static void on_close(struct rdp_dynvc* d, uint32_t id, void* user)
{
	(void)d; (void)id; (void)user;
	got_close = 1;
}

/* wrap a DYNVC PDU in CHANNEL_PDU_HEADER FIRST|LAST */
static int feed_whole(struct rdp_dynvc* d, const uint8_t* pdu, size_t len)
{
	uint8_t buf[2048];
	int n = rdp_vc_build(buf, sizeof(buf), pdu, len, RDP_VC_FLAG_FIRST | RDP_VC_FLAG_LAST);
	CHECK(n > 0, "vc_build");
	return rdp_dynvc_input(d, buf, (size_t)n);
}

static void test_vc_reassembly(void)
{
	struct rdp_vc_rx r;
	uint8_t a[20], b[20];
	rdp_vc_rx_init(&r);
	/* total 10, two chunks of 5 */
	a[0] = 10; a[1] = a[2] = a[3] = 0;
	a[4] = RDP_VC_FLAG_FIRST; a[5] = a[6] = a[7] = 0;
	memcpy(a + 8, "hello", 5);
	CHECK(rdp_vc_rx_feed(&r, a, 13) == 0, "first chunk");
	b[0] = 10; b[1] = b[2] = b[3] = 0;
	b[4] = RDP_VC_FLAG_LAST; b[5] = b[6] = b[7] = 0;
	memcpy(b + 8, "world", 5);
	CHECK(rdp_vc_rx_feed(&r, b, 13) == 1, "last chunk");
	CHECK(r.have == 10 && !memcmp(r.buf, "helloworld", 10), "payload");
	rdp_vc_rx_free(&r);
}

static void test_caps_create_data(void)
{
	struct rdp_dynvc d;
	uint8_t caps[] = { 0x50, 0x00, 0x01, 0x00 }; /* Cmd=CAPS, Pad, Version=1 */
	uint8_t create[32];
	size_t pos;
	uint8_t data_pdu[32];
	const char* name = "Microsoft::Windows::RDS::Graphics";

	nsent = 0;
	got_create = got_close = 0;
	got_data_len = 0;
	rdp_dynvc_init(&d, capture_send, NULL);
	CHECK(rdp_dynvc_listen(&d, name, on_create, on_data, on_close, NULL) == 0, "listen");

	CHECK(feed_whole(&d, caps, sizeof(caps)) == 0, "caps in");
	CHECK(d.caps_ok && d.version == 1, "caps negotiated");
	CHECK(nsent == 1 && sent_len[0] == 4 && (sent[0][0] >> 4) == 5 && sent[0][2] == 1, "caps rsp");

	/* Create: cbId=0 (1-byte id), Cmd=CREATE, ChannelId=7, name + NUL */
	pos = 0;
	create[pos++] = 0x10; /* cbid=0 cmd=1 */
	create[pos++] = 7;
	memcpy(create + pos, name, strlen(name) + 1);
	pos += strlen(name) + 1;
	nsent = 0;
	CHECK(feed_whole(&d, create, pos) == 0, "create in");
	CHECK(got_create && got_id == 7 && !strcmp(got_name, name), "create cb");
	CHECK(nsent == 1 && (sent[0][0] >> 4) == 1 && sent[0][1] == 7, "create rsp id");
	CHECK(sent[0][2] == 0 && sent[0][3] == 0 && sent[0][4] == 0 && sent[0][5] == 0, "create ok");

	/* unfragmented DATA: cmd=DATA, id=7, payload "GFX" */
	pos = 0;
	data_pdu[pos++] = 0x30; /* cbid=0 cmd=3 */
	data_pdu[pos++] = 7;
	memcpy(data_pdu + pos, "GFX", 3);
	pos += 3;
	CHECK(feed_whole(&d, data_pdu, pos) == 0, "data in");
	CHECK(got_data_len == 3 && !memcmp(got_data, "GFX", 3), "data delivered");
	CHECK(d.data_msgs == 1, "data count");

	/* Close */
	nsent = 0;
	data_pdu[0] = 0x40; data_pdu[1] = 7;
	CHECK(feed_whole(&d, data_pdu, 2) == 0, "close in");
	CHECK(got_close, "close cb");
	CHECK(nsent == 1 && (sent[0][0] >> 4) == 4, "close rsp");

	rdp_dynvc_free(&d);
}

static void test_data_first(void)
{
	struct rdp_dynvc d;
	uint8_t create[80], first[40], next[40];
	size_t pos;
	const char* name = "Microsoft::Windows::RDS::Graphics";

	nsent = 0; got_data_len = 0;
	rdp_dynvc_init(&d, capture_send, NULL);
	rdp_dynvc_listen(&d, name, on_create, on_data, on_close, NULL);
	/* caps + create */
	{ uint8_t caps[] = { 0x50, 0x00, 0x01, 0x00 }; feed_whole(&d, caps, 4); }
	pos = 0; create[pos++] = 0x10; create[pos++] = 3;
	memcpy(create + pos, name, strlen(name) + 1); pos += strlen(name) + 1;
	feed_whole(&d, create, pos);

	/* DATA_FIRST: total 5, first 3 bytes "hel"; Len field 1 byte (lenbits=0) */
	pos = 0;
	first[pos++] = 0x20; /* cbid=0 lenbits=0 cmd=2 */
	first[pos++] = 3;    /* channel */
	first[pos++] = 5;    /* total length */
	memcpy(first + pos, "hel", 3); pos += 3;
	CHECK(feed_whole(&d, first, pos) == 0, "data first");
	CHECK(got_data_len == 0, "not complete yet");
	pos = 0;
	next[pos++] = 0x30;
	next[pos++] = 3;
	memcpy(next + pos, "lo", 2); pos += 2;
	CHECK(feed_whole(&d, next, pos) == 0, "data cont");
	CHECK(got_data_len == 5 && !memcmp(got_data, "hello", 5), "reassembled");
	rdp_dynvc_free(&d);
}

int main(void)
{
	test_vc_reassembly();
	test_caps_create_data();
	test_data_first();
	printf("drdynvc: %d passed, %d failed\n", passes, fails);
	return fails ? 1 : 0;
}
