/*
 * GFAmigaRDP host tests: vchan framing and the DYNVC manager ([MS-RDPEDYC]).
 *   cc -I../../src/core -o test_drdynvc test_drdynvc.c \
 *      ../../src/core/vchan.c ../../src/core/drdynvc.c
 * Synthetic PDUs from the public [MS-RDPEDYC] layouts only (CLEAN-ROOM.md).
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vchan.h"
#include "drdynvc.h"

static int fails, passes;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint8_t sent[16][2048];
static size_t sent_len[16];
static int nsent;

static int capture_send(void* user, const uint8_t* pdu, size_t len)
{
	(void)user;
	if (nsent >= 16 || len > sizeof(sent[0]))
		return -1;
	memcpy(sent[nsent], pdu, len);
	sent_len[nsent] = len;
	nsent++;
	return 0;
}

static int got_create;
static uint32_t got_id;
static char got_name[64];
static uint8_t got_data[4096];
static size_t got_data_len;
static int got_close;
static int refuse_create;

static int on_create(struct rdp_dynvc* d, uint32_t id, const char* name, void* user)
{
	(void)d; (void)user;
	got_create = 1;
	got_id = id;
	strncpy(got_name, name, sizeof(got_name) - 1);
	return refuse_create ? -1 : 0;
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
	uint8_t buf[4096];
	int n = rdp_vc_build(buf, sizeof(buf), pdu, len, RDP_VC_FLAG_FIRST | RDP_VC_FLAG_LAST);
	CHECK(n > 0, "vc_build");
	return rdp_dynvc_input(d, buf, (size_t)n);
}

static void reset_cb(void)
{
	got_create = got_close = 0;
	got_data_len = 0;
	got_name[0] = 0;
	refuse_create = 0;
	nsent = 0;
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

static void test_vc_build_roundtrip(void)
{
	uint8_t out[64];
	struct rdp_vc_rx r;
	int n = rdp_vc_build(out, sizeof(out), (const uint8_t*)"ABCD", 4,
	                     RDP_VC_FLAG_FIRST | RDP_VC_FLAG_LAST);
	CHECK(n == 12, "build len");
	CHECK(out[0] == 4 && out[4] == (RDP_VC_FLAG_FIRST | RDP_VC_FLAG_LAST), "hdr");
	rdp_vc_rx_init(&r);
	CHECK(rdp_vc_rx_feed(&r, out, (size_t)n) == 1, "feed built");
	CHECK(r.have == 4 && !memcmp(r.buf, "ABCD", 4), "payload");
	rdp_vc_rx_free(&r);
}

static void test_caps_create_data(void)
{
	struct rdp_dynvc d;
	uint8_t caps[] = { 0x50, 0x00, 0x01, 0x00 }; /* Cmd=CAPS, Pad, Version=1 */
	uint8_t create[80];
	size_t pos;
	uint8_t data_pdu[32];
	const char* name = "Microsoft::Windows::RDS::Graphics";

	reset_cb();
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

static void test_caps_v3_answers_v1(void)
{
	struct rdp_dynvc d;
	/* Cmd=CAPS, Pad, Version=3, PriorityCharge0..3 */
	uint8_t caps[] = {
		0x50, 0x00, 0x03, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	reset_cb();
	rdp_dynvc_init(&d, capture_send, NULL);
	CHECK(feed_whole(&d, caps, sizeof(caps)) == 0, "caps v3 in");
	CHECK(d.caps_ok && d.version == 1, "downgrade to v1");
	CHECK(nsent == 1 && sent[0][2] == 1 && sent[0][3] == 0, "caps rsp v1");
	rdp_dynvc_free(&d);
}

static void test_create_unknown_refused(void)
{
	struct rdp_dynvc d;
	uint8_t create[40];
	size_t pos = 0;
	const char* name = "NoSuchListener";
	reset_cb();
	rdp_dynvc_init(&d, capture_send, NULL);
	{ uint8_t caps[] = { 0x50, 0x00, 0x01, 0x00 }; feed_whole(&d, caps, 4); }
	nsent = 0;
	create[pos++] = 0x10;
	create[pos++] = 9;
	memcpy(create + pos, name, strlen(name) + 1);
	pos += strlen(name) + 1;
	CHECK(feed_whole(&d, create, pos) == 0, "create unknown");
	CHECK(!got_create, "no listener cb");
	CHECK(nsent == 1 && (sent[0][0] >> 4) == 1 && sent[0][1] == 9, "create rsp");
	/* HRESULT E_FAIL = 0x80004005 little-endian */
	CHECK(sent[0][2] == 0x05 && sent[0][3] == 0x40 && sent[0][4] == 0x00 && sent[0][5] == 0x80,
	      "E_FAIL status");
	CHECK(rdp_dynvc_find(&d, 9) == NULL, "not open");
	rdp_dynvc_free(&d);
}

static void test_create_listener_refuses(void)
{
	struct rdp_dynvc d;
	uint8_t create[80];
	size_t pos = 0;
	const char* name = "Microsoft::Windows::RDS::Graphics";
	reset_cb();
	refuse_create = 1;
	rdp_dynvc_init(&d, capture_send, NULL);
	rdp_dynvc_listen(&d, name, on_create, on_data, on_close, NULL);
	{ uint8_t caps[] = { 0x50, 0x00, 0x01, 0x00 }; feed_whole(&d, caps, 4); }
	nsent = 0;
	create[pos++] = 0x10; create[pos++] = 4;
	memcpy(create + pos, name, strlen(name) + 1); pos += strlen(name) + 1;
	CHECK(feed_whole(&d, create, pos) == 0, "create refused");
	CHECK(got_create, "cb saw it");
	CHECK(sent[0][2] == 0x05 && sent[0][5] == 0x80, "E_FAIL");
	rdp_dynvc_free(&d);
}

static void test_data_first(void)
{
	struct rdp_dynvc d;
	uint8_t create[80], first[40], next[40];
	size_t pos;
	const char* name = "Microsoft::Windows::RDS::Graphics";

	reset_cb();
	rdp_dynvc_init(&d, capture_send, NULL);
	rdp_dynvc_listen(&d, name, on_create, on_data, on_close, NULL);
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

static void test_cbid_2byte(void)
{
	struct rdp_dynvc d;
	uint8_t create[80], data_pdu[16];
	size_t pos;
	const char* name = "Microsoft::Windows::RDS::Graphics";
	reset_cb();
	rdp_dynvc_init(&d, capture_send, NULL);
	rdp_dynvc_listen(&d, name, on_create, on_data, on_close, NULL);
	{ uint8_t caps[] = { 0x50, 0x00, 0x01, 0x00 }; feed_whole(&d, caps, 4); }
	/* Create with 2-byte ChannelId 0x1234 (cbId=1) */
	pos = 0;
	create[pos++] = 0x11; /* cbid=1 cmd=1 */
	create[pos++] = 0x34;
	create[pos++] = 0x12;
	memcpy(create + pos, name, strlen(name) + 1);
	pos += strlen(name) + 1;
	nsent = 0;
	CHECK(feed_whole(&d, create, pos) == 0, "create 2byte");
	CHECK(got_id == 0x1234, "id");
	CHECK(nsent == 1 && (sent[0][0] & 3) == 1 && sent[0][1] == 0x34 && sent[0][2] == 0x12, "rsp cbid");
	pos = 0;
	data_pdu[pos++] = 0x31; /* cbid=1 cmd=DATA */
	data_pdu[pos++] = 0x34; data_pdu[pos++] = 0x12;
	memcpy(data_pdu + pos, "OK", 2); pos += 2;
	CHECK(feed_whole(&d, data_pdu, pos) == 0, "data 2byte");
	CHECK(got_data_len == 2 && !memcmp(got_data, "OK", 2), "payload");
	rdp_dynvc_free(&d);
}

static void test_send_data_fragments(void)
{
	struct rdp_dynvc d;
	uint8_t create[80];
	size_t pos;
	uint8_t big[2000];
	const char* name = "Microsoft::Windows::RDS::Graphics";
	int i;
	for (i = 0; i < (int)sizeof(big); i++)
		big[i] = (uint8_t)(i & 0xFF);
	reset_cb();
	rdp_dynvc_init(&d, capture_send, NULL);
	rdp_dynvc_listen(&d, name, on_create, on_data, on_close, NULL);
	{ uint8_t caps[] = { 0x50, 0x00, 0x01, 0x00 }; feed_whole(&d, caps, 4); }
	pos = 0; create[pos++] = 0x10; create[pos++] = 1;
	memcpy(create + pos, name, strlen(name) + 1); pos += strlen(name) + 1;
	feed_whole(&d, create, pos);
	nsent = 0;
	CHECK(rdp_dynvc_send_data(&d, 1, big, sizeof(big)) == 0, "send big");
	CHECK(nsent >= 2, "fragmented");
	CHECK((sent[0][0] >> 4) == 2, "DATA_FIRST");
	CHECK((sent[nsent - 1][0] >> 4) == 3, "last DATA");
	rdp_dynvc_free(&d);
}

static void test_softsync(void)
{
	struct rdp_dynvc d;
	uint8_t req[] = { 0x80, 0x00, 0x00, 0x00, 0x00, 0x00 }; /* Soft-Sync Request stub */
	reset_cb();
	rdp_dynvc_init(&d, capture_send, NULL);
	CHECK(feed_whole(&d, req, sizeof(req)) == 0, "softsync in");
	CHECK(nsent == 1 && (sent[0][0] >> 4) == 9, "softsync rsp");
	CHECK(sent_len[0] == 6 && sent[0][2] == 0 && sent[0][3] == 0, "zero tunnels");
	rdp_dynvc_free(&d);
}

static void test_compressed_rejected(void)
{
	struct rdp_dynvc d;
	uint8_t pdu[] = { 0x60, 0x01, 0x00 }; /* DATA_FIRST_COMP, id 1 */
	reset_cb();
	rdp_dynvc_init(&d, capture_send, NULL);
	CHECK(feed_whole(&d, pdu, sizeof(pdu)) < 0, "comp rejected");
	rdp_dynvc_free(&d);
}

/* review 2: Create (1-byte id) for name; the CREATE_RSP's status, or 1 when none came */
static int32_t create_status(struct rdp_dynvc* d, uint8_t id, const char* name)
{
	uint8_t create[300];
	size_t pos = 0, n = strlen(name) + 1;
	if (n > sizeof(create) - 2)
		return 1;
	create[pos++] = 0x10;
	create[pos++] = id;
	memcpy(create + pos, name, n);
	pos += n;
	nsent = 0;
	if (feed_whole(d, create, pos) < 0 || nsent != 1 || (sent[0][0] >> 4) != 1 || sent[0][1] != id)
		return 1;
	return (int32_t)((uint32_t)sent[0][2] | ((uint32_t)sent[0][3] << 8) | ((uint32_t)sent[0][4] << 16) | ((uint32_t)sent[0][5] << 24));
}

static void close_chan(struct rdp_dynvc* d, uint8_t id)
{
	uint8_t pdu[2] = { 0x40, 0 };
	pdu[1] = id;
	feed_whole(d, pdu, 2);
}

/* review 2 N4: a closed channel's slot is used again */
static void test_slots_reused(void)
{
	struct rdp_dynvc d;
	const char* name = "Microsoft::Windows::RDS::Graphics";
	int i, ok = 1;

	reset_cb();
	rdp_dynvc_init(&d, capture_send, NULL);
	rdp_dynvc_listen(&d, name, on_create, on_data, on_close, NULL);
	{ uint8_t caps[] = { 0x50, 0x00, 0x01, 0x00 }; feed_whole(&d, caps, 4); }
	/* 40 channels one after another, each closed before the next */
	for (i = 1; i <= 40 && ok; i++)
	{
		ok = create_status(&d, (uint8_t)i, name) == 0;
		close_chan(&d, (uint8_t)i);
	}
	CHECK(ok, "create %d after closes refused", i - 1);
	CHECK(d.nchans <= 1, "slots %d", d.nchans);
	/* 16 at once, the 17th refused; one closed, then another fits */
	for (i = 100; i < 116 && ok; i++)
		ok = create_status(&d, (uint8_t)i, name) == 0;
	CHECK(ok, "16 open");
	CHECK(create_status(&d, 116, name) == (int32_t)0x80004005L, "17th: E_FAIL");
	close_chan(&d, 105);
	CHECK(rdp_dynvc_find(&d, 105) == NULL, "105 closed");
	CHECK(create_status(&d, 117, name) == 0, "a closed slot used again");
	CHECK(rdp_dynvc_find(&d, 117) != NULL && rdp_dynvc_find(&d, 110) != NULL, "117 and 110 open");
	rdp_dynvc_free(&d);
}

/* a CHANNEL_PDU_HEADER chunk: length, flags, payload */
static size_t vc_chunk(uint8_t* out, uint32_t total, uint32_t flags, const uint8_t* payload, size_t len)
{
	int i;
	for (i = 0; i < 4; i++)
	{
		out[i] = (uint8_t)(total >> (8 * i));
		out[4 + i] = (uint8_t)(flags >> (8 * i));
	}
	if (len)
		memcpy(out + 8, payload, len);
	return 8 + len;
}

/* review 2 N5: a static channel message bigger than RDP_VC_MAX_MESSAGE is
 * read past and dropped, not allocated, and the next one comes through */
static void test_vc_oversize_dropped(void)
{
	struct rdp_vc_rx r;
	uint8_t c[64];
	size_t n;

	rdp_vc_rx_init(&r);
	n = vc_chunk(c, 0xFFFFFFF0u, RDP_VC_FLAG_FIRST, (const uint8_t*)"0123456789", 10);
	CHECK(rdp_vc_rx_feed(&r, c, n) == 0, "4 GB FIRST: no error");
	CHECK(r.buf == NULL && r.size == 0, "nothing allocated (%lu)", (unsigned long)r.size);
	n = vc_chunk(c, 0xFFFFFFF0u, 0, (const uint8_t*)"abcdefghij", 10);
	CHECK(rdp_vc_rx_feed(&r, c, n) == 0, "middle chunk");
	n = vc_chunk(c, 0xFFFFFFF0u, RDP_VC_FLAG_LAST, (const uint8_t*)"xyz", 3);
	CHECK(rdp_vc_rx_feed(&r, c, n) == 0, "LAST: dropped, not delivered");
	CHECK(r.dropped == 1 && !r.active && r.size == 0, "dropped %u", (unsigned)r.dropped);
	n = vc_chunk(c, (uint32_t)RDP_VC_MAX_MESSAGE + 1, RDP_VC_FLAG_FIRST | RDP_VC_FLAG_LAST, (const uint8_t*)"x", 1);
	CHECK(rdp_vc_rx_feed(&r, c, n) < 0 || r.dropped == 2, "limit + 1 refused");
	CHECK(r.size == 0, "limit + 1 not allocated");
	n = vc_chunk(c, 5, RDP_VC_FLAG_FIRST | RDP_VC_FLAG_LAST, (const uint8_t*)"after", 5);
	CHECK(rdp_vc_rx_feed(&r, c, n) == 1 && r.have == 5 && !memcmp(r.buf, "after", 5), "next message whole");
	rdp_vc_rx_free(&r);
}

/* review 2 N5: a DVC DATA_FIRST naming more than RDP_VC_MAX_MESSAGE is
 * read past and dropped; the channel and the session go on */
static void test_dvc_oversize_dropped(void)
{
	struct rdp_dynvc d;
	const char* name = "Microsoft::Windows::RDS::Graphics";
	uint8_t pdu[64];
	size_t pos;

	reset_cb();
	rdp_dynvc_init(&d, capture_send, NULL);
	rdp_dynvc_listen(&d, name, on_create, on_data, on_close, NULL);
	{ uint8_t caps[] = { 0x50, 0x00, 0x01, 0x00 }; feed_whole(&d, caps, 4); }
	CHECK(create_status(&d, 4, name) == 0, "create");
	/* DATA_FIRST, 4-byte Len (lenbits 2): 0xFFFFFFFF, 3 bytes now */
	pos = 0;
	pdu[pos++] = 0x28; /* cbid=0 lenbits=2 cmd=2 */
	pdu[pos++] = 4;
	pdu[pos++] = 0xFF; pdu[pos++] = 0xFF; pdu[pos++] = 0xFF; pdu[pos++] = 0xFF;
	memcpy(pdu + pos, "abc", 3); pos += 3;
	CHECK(feed_whole(&d, pdu, pos) == 0, "4 GB DATA_FIRST: no error");
	CHECK(rdp_dynvc_find(&d, 4) && rdp_dynvc_find(&d, 4)->asm_buf == NULL, "nothing allocated");
	pos = 0;
	pdu[pos++] = 0x30; pdu[pos++] = 4;
	memcpy(pdu + pos, "defg", 4); pos += 4;
	CHECK(feed_whole(&d, pdu, pos) == 0, "DATA after it: read past");
	CHECK(got_data_len == 0 && d.data_msgs == 0, "nothing delivered");
	/* just over the limit, finished: dropped, and the channel still works */
	pos = 0;
	pdu[pos++] = 0x28; pdu[pos++] = 4;
	{
		uint32_t t = (uint32_t)RDP_VC_MAX_MESSAGE + 1;
		pdu[pos++] = (uint8_t)t; pdu[pos++] = (uint8_t)(t >> 8); pdu[pos++] = (uint8_t)(t >> 16); pdu[pos++] = (uint8_t)(t >> 24);
	}
	CHECK(feed_whole(&d, pdu, pos) == 0, "limit + 1: no error");
	{
		/* the rest of it, in DATA PDUs of 2000 bytes */
		static uint8_t big[2002];
		size_t left = (size_t)RDP_VC_MAX_MESSAGE + 1;
		int ok = 1;
		uint8_t vc[2100];
		big[0] = 0x30; big[1] = 4;
		while (left && ok)
		{
			size_t k = left > 2000 ? 2000 : left;
			int n = rdp_vc_build(vc, sizeof(vc), big, 2 + k, RDP_VC_FLAG_FIRST | RDP_VC_FLAG_LAST);
			ok = n > 0 && rdp_dynvc_input(&d, vc, (size_t)n) == 0;
			left -= k;
		}
		CHECK(ok, "the rest read past");
	}
	CHECK(d.dropped == 1 && d.data_msgs == 0, "dropped %u", (unsigned)d.dropped);
	pos = 0;
	pdu[pos++] = 0x30; pdu[pos++] = 4;
	memcpy(pdu + pos, "ok", 2); pos += 2;
	CHECK(feed_whole(&d, pdu, pos) == 0, "next message");
	CHECK(got_data_len == 2 && !memcmp(got_data, "ok", 2) && d.data_msgs == 1, "delivered after the drop");
	rdp_dynvc_free(&d);
}

/* review 2 N6: a channel name of 64 bytes or more is refused with E_FAIL
 * for that channel only; the session and the next create go on */
static void test_long_name_refused(void)
{
	struct rdp_dynvc d;
	const char* name = "Microsoft::Windows::RDS::Graphics";
	char longname[200];
	uint8_t pdu[8];
	static const int lens[] = { 64, 65, 100, 150 };
	unsigned i;
	int n;

	reset_cb();
	rdp_dynvc_init(&d, capture_send, NULL);
	rdp_dynvc_listen(&d, name, on_create, on_data, on_close, NULL);
	{ uint8_t caps[] = { 0x50, 0x00, 0x01, 0x00 }; feed_whole(&d, caps, 4); }
	for (i = 0; i < sizeof(lens) / sizeof(lens[0]); i++)
	{
		n = lens[i];
		memset(longname, 'A', sizeof(longname));
		memcpy(longname, name, strlen(name));   /* a listener's name as its start */
		longname[n] = 0;
		got_create = 0;
		CHECK(create_status(&d, (uint8_t)n, longname) == (int32_t)0x80004005L, "%d-byte name: E_FAIL", n);
		CHECK(!got_create && rdp_dynvc_find(&d, (uint32_t)n) == NULL, "%d-byte name: not opened", n);
	}
	/* a name with no NUL at all is still a broken PDU */
	pdu[0] = 0x10; pdu[1] = 9; pdu[2] = 'x'; pdu[3] = 'y';
	CHECK(feed_whole(&d, pdu, 4) < 0, "no NUL: protocol error");
	CHECK(create_status(&d, 5, name) == 0, "a good create after them");
	CHECK(rdp_dynvc_find(&d, 5) != NULL, "open");
	rdp_dynvc_free(&d);
}

int main(void)
{
	test_vc_reassembly();
	test_vc_build_roundtrip();
	test_caps_create_data();
	test_caps_v3_answers_v1();
	test_create_unknown_refused();
	test_create_listener_refuses();
	test_data_first();
	test_cbid_2byte();
	test_send_data_fragments();
	test_softsync();
	test_compressed_rejected();
	test_slots_reused();
	test_vc_oversize_dropped();
	test_dvc_oversize_dropped();
	test_long_name_refused();
	printf("drdynvc: %d passed, %d failed\n", passes, fails);
	return fails ? 1 : 0;
}
