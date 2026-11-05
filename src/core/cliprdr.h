/*
 * GFAmigaRDP: Clipboard Virtual Channel ([MS-RDPECLIP]) on the static
 * "cliprdr" channel. Text only for now: CF_UNICODETEXT (13) and CF_TEXT (1).
 * File streams, metafiles and palettes are refused. See cliprdr.c.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_CLIPRDR_H
#define RDP_CLIPRDR_H

#include <stddef.h>
#include <stdint.h>

#include "vchan.h"

/* Windows standard clipboard format IDs used in [MS-RDPECLIP] examples. */
#define RDP_CF_TEXT         1u
#define RDP_CF_UNICODETEXT  13u

#define RDP_CB_MONITOR_READY          0x0001u
#define RDP_CB_FORMAT_LIST            0x0002u
#define RDP_CB_FORMAT_LIST_RESPONSE   0x0003u
#define RDP_CB_FORMAT_DATA_REQUEST    0x0004u
#define RDP_CB_FORMAT_DATA_RESPONSE   0x0005u
#define RDP_CB_CLIP_CAPS              0x0007u

#define RDP_CB_RESPONSE_OK    0x0001u
#define RDP_CB_RESPONSE_FAIL  0x0002u
#define RDP_CB_ASCII_NAMES    0x0004u

#define RDP_CB_CAPSTYPE_GENERAL       0x0001u
#define RDP_CB_CAPS_VERSION_2         0x00000002u
#define RDP_CB_USE_LONG_FORMAT_NAMES  0x00000002u

/* Cap text we will send or accept (UTF-8 bytes, excluding NUL). */
#define RDP_CLIP_MAX_TEXT (256u * 1024u)

struct rdp_cliprdr;

/* Send one complete CLIPRDR PDU (no CHANNEL_PDU_HEADER). 0 or < 0. */
typedef int (*rdp_cliprdr_send_fn)(void* user, const uint8_t* pdu, size_t len);

/* Remote text arrived (UTF-8, NUL-terminated). Do not free text. */
typedef void (*rdp_cliprdr_on_text_fn)(void* user, const char* utf8);

struct rdp_cliprdr
{
	rdp_cliprdr_send_fn send;
	rdp_cliprdr_on_text_fn on_text;
	void* user;

	struct rdp_vc_rx rx;     /* CHANNEL_PDU_HEADER reassembly */
	uint32_t peer_flags;     /* generalFlags from the peer's Caps */
	uint32_t our_flags;      /* what we advertised */
	uint8_t inited;          /* 1: Monitor Ready handled, our Caps sent */
	uint8_t long_names;      /* 1: both ends have USE_LONG_FORMAT_NAMES */
	uint8_t we_own;          /* 1: we announced formats; serve Format Data Request */
	uint8_t ignore_echo;     /* 1: next local announce is from our own write */

	uint32_t remote_fmts[8];
	int n_remote;

	char* local_utf8;        /* text we offer (malloc); wiped on replace */
	size_t local_len;

	uint32_t caps_ok;        /* Caps PDUs exchanged */
	uint32_t lists_ok;       /* Format List Responses OK we sent or got */
	uint32_t pastes;         /* Format Data Responses with text we accepted */
	uint32_t copies;         /* Format Lists we sent */
	uint32_t fails;          /* protocol / send failures counted */
	uint32_t pending_req;    /* Format Data Request id awaiting response, or 0 */
};

void rdp_cliprdr_init(struct rdp_cliprdr* c, rdp_cliprdr_send_fn send,
                      rdp_cliprdr_on_text_fn on_text, void* user);
void rdp_cliprdr_free(struct rdp_cliprdr* c);

/* Feed one MCS userData that starts with CHANNEL_PDU_HEADER. 0, 1 (message
 * handled), or < 0 on a hard error (session may continue on soft fails). */
int rdp_cliprdr_input(struct rdp_cliprdr* c, const uint8_t* data, size_t len);

/* Announce local UTF-8 text to the peer (Format List). Empty or NULL clears
 * ownership without sending. 0 or < 0. Skipped while ignore_echo is set
 * (cleared after one call). Never call with a password. */
int rdp_cliprdr_set_local_text(struct rdp_cliprdr* c, const char* utf8);

/* Host helpers for unit tests: build / parse without a live channel. */
int rdp_cliprdr_build_header(uint8_t* out, size_t size, uint16_t type, uint16_t flags, uint32_t data_len);
int rdp_cliprdr_build_caps(uint8_t* out, size_t size, uint32_t general_flags);
int rdp_cliprdr_build_format_list_text(uint8_t* out, size_t size, int long_names);
int rdp_cliprdr_utf16le_to_utf8(char* dst, size_t dst_size, const uint8_t* u16, size_t u16_len);
int rdp_cliprdr_utf8_to_utf16le_z(uint8_t* dst, size_t dst_size, const char* utf8);

#endif
