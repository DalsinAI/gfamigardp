/*
 * GFAmigaRDP: Clipboard Virtual Channel ([MS-RDPECLIP]).
 *
 * Sections implemented:
 *   2.1 transport (static "cliprdr")
 *   2.2.1 CLIPRDR_HEADER
 *   2.2.2.1 Clipboard Capabilities / General Capability Set
 *   2.2.2.2 Monitor Ready
 *   2.2.3.1 Format List (short and long names)
 *   2.2.3.2 Format List Response
 *   2.2.5.1 Format Data Request
 *   2.2.5.2 Format Data Response (generic text payloads)
 *   1.3.2.1 Initialization, 1.3.2.2.1 Copy, 1.3.2.2.3 Paste
 *   3.2.5.1 client processing of Caps and Monitor Ready
 *
 * Text only: CF_UNICODETEXT (13) and CF_TEXT (1). File / metafile / palette
 * requests get CB_RESPONSE_FAIL. Clean-room: Microsoft Open Specifications
 * only (CLEAN-ROOM.md); no FreeRDP.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "cliprdr.h"
#include "utf16.h"
#include "connect.h"

#include <stdlib.h>
#include <string.h>

static uint16_t ru16(const uint8_t* p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t ru32(const uint8_t* p)
{
	return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static void wu16(uint8_t* p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void wu32(uint8_t* p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

void rdp_cliprdr_init(struct rdp_cliprdr* c, rdp_cliprdr_send_fn send,
                      rdp_cliprdr_on_text_fn on_text, void* user)
{
	memset(c, 0, sizeof(*c));
	c->send = send;
	c->on_text = on_text;
	c->user = user;
	c->our_flags = RDP_CB_USE_LONG_FORMAT_NAMES;
	rdp_vc_rx_init(&c->rx);
}

void rdp_cliprdr_free(struct rdp_cliprdr* c)
{
	if (!c)
		return;
	if (c->local_utf8)
	{
		memset(c->local_utf8, 0, c->local_len);
		free(c->local_utf8);
		c->local_utf8 = NULL;
	}
	rdp_vc_rx_free(&c->rx);
	c->local_len = 0;
	c->we_own = 0;
}

int rdp_cliprdr_build_header(uint8_t* out, size_t size, uint16_t type, uint16_t flags, uint32_t data_len)
{
	if (!out || size < 8)
		return RDP_E_SPACE;
	wu16(out, type);
	wu16(out + 2, flags);
	wu32(out + 4, data_len);
	return 8;
}

int rdp_cliprdr_build_caps(uint8_t* out, size_t size, uint32_t general_flags)
{
	/* CLIPRDR_CAPS: header + cCapabilitiesSets(2) + pad(2) + one GENERAL set (12) */
	if (!out || size < 8 + 4 + 12)
		return RDP_E_SPACE;
	rdp_cliprdr_build_header(out, size, RDP_CB_CLIP_CAPS, 0, 4 + 12);
	wu16(out + 8, 1);
	wu16(out + 10, 0);
	wu16(out + 12, RDP_CB_CAPSTYPE_GENERAL);
	wu16(out + 14, 12);
	wu32(out + 16, RDP_CB_CAPS_VERSION_2);
	wu32(out + 20, general_flags);
	return 8 + 4 + 12;
}

int rdp_cliprdr_build_format_list_text(uint8_t* out, size_t size, int long_names)
{
	/* Offer CF_UNICODETEXT and CF_TEXT. Names empty (standard formats). */
	size_t need;
	int n;
	if (long_names)
	{
		/* two long names: formatId + UTF-16 NUL (2 bytes) each = 6+6 */
		need = 8 + 12;
		if (!out || size < need)
			return RDP_E_SPACE;
		rdp_cliprdr_build_header(out, size, RDP_CB_FORMAT_LIST, 0, 12);
		wu32(out + 8, RDP_CF_UNICODETEXT);
		wu16(out + 12, 0);
		wu32(out + 14, RDP_CF_TEXT);
		wu16(out + 18, 0);
		return (int)need;
	}
	/* short names: 36 bytes each, ASCII empty */
	need = 8 + 36 * 2;
	if (!out || size < need)
		return RDP_E_SPACE;
	rdp_cliprdr_build_header(out, size, RDP_CB_FORMAT_LIST, RDP_CB_ASCII_NAMES, 72);
	memset(out + 8, 0, 72);
	wu32(out + 8, RDP_CF_UNICODETEXT);
	wu32(out + 8 + 36, RDP_CF_TEXT);
	return (int)need;
	(void)n;
}

int rdp_cliprdr_utf16le_to_utf8(char* dst, size_t dst_size, const uint8_t* u16, size_t u16_len)
{
	size_t i = 0, n = 0;
	if (!dst || dst_size == 0)
		return RDP_E_SPACE;
	dst[0] = 0;
	if (!u16)
		return 0;
	/* trim a trailing UTF-16 NUL if present */
	if (u16_len >= 2 && u16[u16_len - 2] == 0 && u16[u16_len - 1] == 0)
		u16_len -= 2;
	while (i + 1 < u16_len)
	{
		uint32_t c = (uint32_t)(u16[i] | (u16[i + 1] << 8));
		i += 2;
		if (c >= 0xd800 && c <= 0xdbff && i + 1 < u16_len)
		{
			uint32_t lo = (uint32_t)(u16[i] | (u16[i + 1] << 8));
			if (lo >= 0xdc00 && lo <= 0xdfff)
			{
				c = 0x10000 + (((c & 0x3ff) << 10) | (lo & 0x3ff));
				i += 2;
			}
			else
				c = 0xfffd;
		}
		else if (c >= 0xdc00 && c <= 0xdfff)
			c = 0xfffd;
		if (c < 0x80)
		{
			if (n + 1 >= dst_size)
				break;
			dst[n++] = (char)c;
		}
		else if (c < 0x800)
		{
			if (n + 2 >= dst_size)
				break;
			dst[n++] = (char)(0xc0 | (c >> 6));
			dst[n++] = (char)(0x80 | (c & 0x3f));
		}
		else if (c < 0x10000)
		{
			if (n + 3 >= dst_size)
				break;
			dst[n++] = (char)(0xe0 | (c >> 12));
			dst[n++] = (char)(0x80 | ((c >> 6) & 0x3f));
			dst[n++] = (char)(0x80 | (c & 0x3f));
		}
		else
		{
			if (n + 4 >= dst_size)
				break;
			dst[n++] = (char)(0xf0 | (c >> 18));
			dst[n++] = (char)(0x80 | ((c >> 12) & 0x3f));
			dst[n++] = (char)(0x80 | ((c >> 6) & 0x3f));
			dst[n++] = (char)(0x80 | (c & 0x3f));
		}
	}
	dst[n] = 0;
	return (int)n;
}

int rdp_cliprdr_utf8_to_utf16le_z(uint8_t* dst, size_t dst_size, const char* utf8)
{
	size_t n;
	if (!dst || dst_size < 2)
		return RDP_E_SPACE;
	n = rdp_utf16_put(utf8 ? utf8 : "", dst, dst_size - 2);
	dst[n] = 0;
	dst[n + 1] = 0;
	return (int)(n + 2);
}

static int send_pdu(struct rdp_cliprdr* c, const uint8_t* pdu, size_t len)
{
	int rc;
	if (!c->send)
		return RDP_E_PROTO;
	rc = c->send(c->user, pdu, len);
	if (rc < 0)
		c->fails++;
	return rc;
}

static int send_simple(struct rdp_cliprdr* c, uint16_t type, uint16_t flags)
{
	uint8_t pdu[8];
	rdp_cliprdr_build_header(pdu, sizeof(pdu), type, flags, 0);
	return send_pdu(c, pdu, 8);
}

static int send_our_caps(struct rdp_cliprdr* c)
{
	uint8_t pdu[32];
	int n = rdp_cliprdr_build_caps(pdu, sizeof(pdu), c->our_flags);
	if (n < 0)
		return n;
	return send_pdu(c, pdu, (size_t)n);
}

static int send_format_list(struct rdp_cliprdr* c)
{
	uint8_t pdu[128];
	int n = rdp_cliprdr_build_format_list_text(pdu, sizeof(pdu), c->long_names);
	if (n < 0)
		return n;
	c->copies++;
	c->we_own = 1;
	return send_pdu(c, pdu, (size_t)n);
}

static int request_best_text(struct rdp_cliprdr* c)
{
	uint8_t pdu[12];
	uint32_t id = 0;
	int i;
	for (i = 0; i < c->n_remote; i++)
		if (c->remote_fmts[i] == RDP_CF_UNICODETEXT)
		{
			id = RDP_CF_UNICODETEXT;
			break;
		}
	if (!id)
		for (i = 0; i < c->n_remote; i++)
			if (c->remote_fmts[i] == RDP_CF_TEXT)
			{
				id = RDP_CF_TEXT;
				break;
			}
	if (!id)
		return 0;
	rdp_cliprdr_build_header(pdu, sizeof(pdu), RDP_CB_FORMAT_DATA_REQUEST, 0, 4);
	wu32(pdu + 8, id);
	c->pending_req = id;
	return send_pdu(c, pdu, 12);
}

static void remember_remote_fmt(struct rdp_cliprdr* c, uint32_t id)
{
	int i;
	if (c->n_remote >= (int)(sizeof(c->remote_fmts) / sizeof(c->remote_fmts[0])))
		return;
	for (i = 0; i < c->n_remote; i++)
		if (c->remote_fmts[i] == id)
			return;
	c->remote_fmts[c->n_remote++] = id;
}

static int parse_format_list(struct rdp_cliprdr* c, const uint8_t* data, size_t len, uint16_t flags)
{
	c->n_remote = 0;
	if (c->long_names)
	{
		size_t i = 0;
		while (i + 4 <= len)
		{
			uint32_t id = ru32(data + i);
			i += 4;
			remember_remote_fmt(c, id);
			/* skip UTF-16LE name through NUL */
			while (i + 1 < len)
			{
				uint16_t u = ru16(data + i);
				i += 2;
				if (u == 0)
					break;
			}
		}
	}
	else
	{
		size_t i = 0;
		size_t step = 36;
		(void)flags;
		while (i + step <= len)
		{
			remember_remote_fmt(c, ru32(data + i));
			i += step;
		}
	}
	return 0;
}

static int serve_format_data(struct rdp_cliprdr* c, uint32_t req_id)
{
	uint8_t* pdu;
	size_t cap, body;
	int n, rc;

	if (!c->we_own || !c->local_utf8)
		return send_simple(c, RDP_CB_FORMAT_DATA_RESPONSE, RDP_CB_RESPONSE_FAIL);

	if (req_id == RDP_CF_UNICODETEXT)
	{
		body = rdp_utf16_len(c->local_utf8) + 2;
		cap = 8 + body;
		pdu = (uint8_t*)malloc(cap);
		if (!pdu)
			return send_simple(c, RDP_CB_FORMAT_DATA_RESPONSE, RDP_CB_RESPONSE_FAIL);
		rdp_cliprdr_build_header(pdu, cap, RDP_CB_FORMAT_DATA_RESPONSE, RDP_CB_RESPONSE_OK, (uint32_t)body);
		n = rdp_cliprdr_utf8_to_utf16le_z(pdu + 8, body, c->local_utf8);
		if (n < 0)
		{
			free(pdu);
			return send_simple(c, RDP_CB_FORMAT_DATA_RESPONSE, RDP_CB_RESPONSE_FAIL);
		}
		rc = send_pdu(c, pdu, 8 + (size_t)n);
		free(pdu);
		return rc;
	}
	if (req_id == RDP_CF_TEXT)
	{
		/* Latin-1 best-effort from UTF-8; NUL-terminated */
		size_t i, o = 0;
		const uint8_t* p = (const uint8_t*)c->local_utf8;
		body = c->local_len + 1;
		if (body > RDP_CLIP_MAX_TEXT + 1)
			body = RDP_CLIP_MAX_TEXT + 1;
		cap = 8 + body;
		pdu = (uint8_t*)malloc(cap);
		if (!pdu)
			return send_simple(c, RDP_CB_FORMAT_DATA_RESPONSE, RDP_CB_RESPONSE_FAIL);
		rdp_cliprdr_build_header(pdu, cap, RDP_CB_FORMAT_DATA_RESPONSE, RDP_CB_RESPONSE_OK, (uint32_t)body);
		while (*p && o + 1 < body)
		{
			uint32_t cp = rdp_utf8_next(&p);
			pdu[8 + o++] = (uint8_t)(cp < 0x100 ? cp : '?');
		}
		pdu[8 + o] = 0;
		rc = send_pdu(c, pdu, 8 + o + 1);
		free(pdu);
		(void)i;
		return rc;
	}
	return send_simple(c, RDP_CB_FORMAT_DATA_RESPONSE, RDP_CB_RESPONSE_FAIL);
}

static int on_data_response(struct rdp_cliprdr* c, uint16_t flags, const uint8_t* data, size_t len)
{
	char* utf8;
	int n;
	uint32_t prefer = c->pending_req;

	c->pending_req = 0;
	if (!(flags & RDP_CB_RESPONSE_OK) || !data || !len)
		return 0;

	utf8 = (char*)malloc(RDP_CLIP_MAX_TEXT + 1);
	if (!utf8)
		return RDP_E_SPACE;

	if (prefer == RDP_CF_TEXT)
	{
		char* tmp = (char*)malloc(RDP_CLIP_MAX_TEXT + 1);
		size_t L = len;
		if (!tmp) { free(utf8); return RDP_E_SPACE; }
		if (L && data[L - 1] == 0)
			L--;
		if (L > RDP_CLIP_MAX_TEXT)
			L = RDP_CLIP_MAX_TEXT;
		memcpy(tmp, data, L);
		tmp[L] = 0;
		n = (int)rdp_latin1_to_utf8(utf8, RDP_CLIP_MAX_TEXT + 1, tmp);
		free(tmp);
	}
	else
		n = rdp_cliprdr_utf16le_to_utf8(utf8, RDP_CLIP_MAX_TEXT + 1, data, len);

	if (n < 0 || !utf8[0])
	{
		free(utf8);
		return 0;
	}
	c->pastes++;
	c->ignore_echo = 1;   /* writing this to the Amiga must not bounce back */
	if (c->on_text)
		c->on_text(c->user, utf8);
	free(utf8);
	return 0;
}

static int handle_pdu(struct rdp_cliprdr* c, const uint8_t* pdu, size_t len)
{
	uint16_t type, flags;
	uint32_t dlen;
	const uint8_t* data;

	if (len < 8)
		return RDP_E_PROTO;
	type = ru16(pdu);
	flags = ru16(pdu + 2);
	dlen = ru32(pdu + 4);
	if (8 + (size_t)dlen > len)
		return RDP_E_PROTO;
	data = pdu + 8;

	switch (type)
	{
		case RDP_CB_CLIP_CAPS:
		{
			uint16_t nsets, i, off;
			if (dlen < 4)
				break;
			nsets = ru16(data);
			off = 4;
			for (i = 0; i < nsets && off + 4 <= dlen; i++)
			{
				uint16_t st = ru16(data + off);
				uint16_t sl = ru16(data + off + 2);
				if (sl < 4 || off + sl > dlen)
					break;
				if (st == RDP_CB_CAPSTYPE_GENERAL && sl >= 12)
					c->peer_flags = ru32(data + off + 8);
				off = (uint16_t)(off + sl);
			}
			c->caps_ok++;
			c->long_names = (c->our_flags & RDP_CB_USE_LONG_FORMAT_NAMES) &&
			                (c->peer_flags & RDP_CB_USE_LONG_FORMAT_NAMES);
			break;
		}
		case RDP_CB_MONITOR_READY:
			/* 3.2.5.1.2: Caps then Format List (text formats; empty local FAILS data) */
			if (send_our_caps(c) < 0)
				return -1;
			c->inited = 1;
			c->long_names = (c->our_flags & RDP_CB_USE_LONG_FORMAT_NAMES) &&
			                (c->peer_flags & RDP_CB_USE_LONG_FORMAT_NAMES);
			if (send_format_list(c) < 0)
				return -1;
			break;
		case RDP_CB_FORMAT_LIST:
			parse_format_list(c, data, dlen, flags);
			if (send_simple(c, RDP_CB_FORMAT_LIST_RESPONSE, RDP_CB_RESPONSE_OK) < 0)
				return -1;
			c->lists_ok++;
			c->we_own = 0;   /* peer is Shared Owner now */
			if (request_best_text(c) < 0)
				return -1;
			break;
		case RDP_CB_FORMAT_LIST_RESPONSE:
			if (flags & RDP_CB_RESPONSE_OK)
				c->lists_ok++;
			break;
		case RDP_CB_FORMAT_DATA_REQUEST:
			if (dlen < 4)
				return send_simple(c, RDP_CB_FORMAT_DATA_RESPONSE, RDP_CB_RESPONSE_FAIL);
			return serve_format_data(c, ru32(data));
		case RDP_CB_FORMAT_DATA_RESPONSE:
			return on_data_response(c, flags, data, dlen);
		default:
			/* Temp dir, file contents, lock: ignore */
			break;
	}
	return 0;
}

int rdp_cliprdr_input(struct rdp_cliprdr* c, const uint8_t* data, size_t len)
{
	int rc;
	if (!c || !data)
		return RDP_E_PROTO;
	rc = rdp_vc_rx_feed(&c->rx, data, len);
	if (rc < 0)
	{
		c->fails++;
		return rc;
	}
	if (rc == 1)
	{
		rc = handle_pdu(c, c->rx.buf, c->rx.have);
		c->rx.have = 0;
		return rc < 0 ? rc : 1;
	}
	return 0;
}

int rdp_cliprdr_set_local_text(struct rdp_cliprdr* c, const char* utf8)
{
	size_t n;
	char* copy;

	if (!c)
		return RDP_E_PROTO;
	if (c->ignore_echo)
	{
		c->ignore_echo = 0;
		return 0;
	}
	if (!c->inited)
	{
		/* stash until Monitor Ready */
		if (c->local_utf8)
		{
			memset(c->local_utf8, 0, c->local_len);
			free(c->local_utf8);
			c->local_utf8 = NULL;
			c->local_len = 0;
		}
		if (utf8 && utf8[0])
		{
			n = strlen(utf8);
			if (n > RDP_CLIP_MAX_TEXT)
				n = RDP_CLIP_MAX_TEXT;
			copy = (char*)malloc(n + 1);
			if (!copy)
				return RDP_E_SPACE;
			memcpy(copy, utf8, n);
			copy[n] = 0;
			c->local_utf8 = copy;
			c->local_len = n;
		}
		return 0;
	}
	if (c->local_utf8)
	{
		memset(c->local_utf8, 0, c->local_len);
		free(c->local_utf8);
		c->local_utf8 = NULL;
		c->local_len = 0;
	}
	if (!utf8 || !utf8[0])
	{
		c->we_own = 0;
		return 0;
	}
	n = strlen(utf8);
	if (n > RDP_CLIP_MAX_TEXT)
		n = RDP_CLIP_MAX_TEXT;
	copy = (char*)malloc(n + 1);
	if (!copy)
		return RDP_E_SPACE;
	memcpy(copy, utf8, n);
	copy[n] = 0;
	c->local_utf8 = copy;
	c->local_len = n;
	return send_format_list(c);
}
