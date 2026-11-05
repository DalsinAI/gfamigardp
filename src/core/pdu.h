/*
 * GFAmigaRDP: the Capabilities Exchange and Connection Finalization PDUs
 * ([MS-RDPBCGR] 1.3.1.1 phases 9 and 10, 2.2.1.13 to 2.2.1.22) as bytes:
 * built and read here with no I/O, so the host tests can check them. With
 * TLS or NLA (ENCRYPTION_LEVEL_NONE) these carry no security header.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_PDU_H
#define RDP_PDU_H

#include <stddef.h>
#include <stdint.h>
#include "stream.h"

#define RDP_E_PROTO_PDU (-100)   /* RDP_E_PROTO (x224.h): not the PDU it should be */

/* TS_SHARECONTROLHEADER pduType (2.2.8.1.1.1.1), version bits included */
#define RDP_PDUTYPE_DEMANDACTIVE 0x1
#define RDP_PDUTYPE_CONFIRMACTIVE 0x3
#define RDP_PDUTYPE_DEACTIVATEALL 0x6
#define RDP_PDUTYPE_DATA 0x7
#define RDP_PDUTYPE_SERVER_REDIR 0xA
#define RDP_PDUTYPE_VERSION 0x10

/* TS_SHAREDATAHEADER pduType2 */
#define RDP_PDUTYPE2_UPDATE 2
#define RDP_PDUTYPE2_CONTROL 20
#define RDP_PDUTYPE2_POINTER 27
#define RDP_PDUTYPE2_SYNCHRONIZE 31
#define RDP_PDUTYPE2_PLAY_SOUND 34
#define RDP_PDUTYPE2_SHUTDOWN_DENIED 37
#define RDP_PDUTYPE2_SAVE_SESSION_INFO 38
#define RDP_PDUTYPE2_FONTLIST 39
#define RDP_PDUTYPE2_FONTMAP 40
#define RDP_PDUTYPE2_SET_ERROR_INFO 47
#define RDP_PDUTYPE2_MONITOR_LAYOUT 55

#define RDP_CTRLACTION_REQUEST_CONTROL 1
#define RDP_CTRLACTION_GRANTED_CONTROL 2
#define RDP_CTRLACTION_DETACH 3
#define RDP_CTRLACTION_COOPERATE 4

#define RDP_SERVER_CHANNEL_ID 0x03EA   /* originatorId and the Synchronize targetUser */

/* what the server's Demand Active said */
struct rdp_demand
{
	uint32_t share_id;
	uint16_t source;            /* pduSource */
	uint16_t ncaps;
	uint32_t cap_mask;          /* bit n: capability set type n (n < 32) was there */
	uint16_t width, height;     /* the Bitmap capability set's desktop size */
	uint16_t bpp;               /* its preferredBitsPerPixel */
	uint32_t session_id;
};

/* one slow-path share PDU read from an MCS Send Data Indication's data */
struct rdp_share
{
	uint16_t type;              /* pduType & 0x0F, or 0 for a flow PDU */
	uint16_t source;
	uint32_t share_id;
	uint8_t type2;              /* data PDUs: pduType2 */
	uint16_t action;            /* Control: action */
	uint32_t error_info;        /* Set Error Info */
	const uint8_t* body;        /* after the share data header (data PDUs) or */
	size_t body_len;            /* after the share control header (the others) */
};

/* A share PDU from s (an SDIN's data). 0, or RDP_E_PROTO */
int rdp_share_parse(const uint8_t* p, size_t len, struct rdp_share* out);
/* The Demand Active's fields from its body (after the share control header) */
int rdp_demand_parse(const uint8_t* p, size_t len, struct rdp_demand* d);

/* Confirm Active (2.2.1.13.2) into s: the client's capability sets for a
 * desktop of w x h at bpp, user the MCS user channel. Its length, or < 0 */
int rdp_build_confirm_active(struct rdp_stream* s, const struct rdp_demand* d, uint16_t user,
                             uint16_t w, uint16_t h, uint16_t bpp, uint32_t keyboard_layout);
/* A data PDU (2.2.8.1.1.1.2) around payload: Synchronize, Control, Font List */
int rdp_build_data(struct rdp_stream* s, uint32_t share_id, uint16_t user, uint8_t type2,
                   const uint8_t* payload, size_t len);
/* the four PDUs the client sends after Confirm Active (2.2.1.14 to 2.2.1.18),
 * each built into its own buffer by the caller with these payloads */
void rdp_payload_sync(uint8_t out[4]);                      /* messageType 1, targetUser */
void rdp_payload_control(uint8_t out[8], uint16_t action);  /* action, grantId 0, controlId 0 */
void rdp_payload_fontlist(uint8_t out[8]);                  /* 0 fonts, FONTLIST_FIRST|LAST, entry size 50 */

/* the capability set types this client sends, in the order it sends them */
extern const uint16_t rdp_client_cap_types[];
extern const int rdp_client_ncaps;

/* fast-path output updates (2.2.9.1.2): counts by updateCode */
struct rdp_fp_counts
{
	uint32_t pdus, updates, bytes;
	uint32_t by_code[16];
};
int rdp_fastpath_count(const uint8_t* p, size_t len, struct rdp_fp_counts* c);

#endif
