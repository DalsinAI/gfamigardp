/*
 * GFAmigaRDP: the Dynamic Virtual Channel manager on the DRDYNVC static
 * channel ([MS-RDPEDYC]): capabilities, create/close, and data reassembly.
 * Plain C99. Listeners register by ANSI name; create requests are accepted
 * or refused, and complete messages are delivered to the listener.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_DRDYNVC_H
#define RDP_DRDYNVC_H

#include <stddef.h>
#include <stdint.h>
#include "vchan.h"

#define RDP_DYNVC_CMD_CREATE 0x01
#define RDP_DYNVC_CMD_DATA_FIRST 0x02
#define RDP_DYNVC_CMD_DATA 0x03
#define RDP_DYNVC_CMD_CLOSE 0x04
#define RDP_DYNVC_CMD_CAPS 0x05
#define RDP_DYNVC_CMD_DATA_FIRST_COMP 0x06
#define RDP_DYNVC_CMD_DATA_COMP 0x07
#define RDP_DYNVC_CMD_SOFTSYNC_REQ 0x08
#define RDP_DYNVC_CMD_SOFTSYNC_RSP 0x09

#define RDP_DYNVC_MAX_CHANNELS 16
#define RDP_DYNVC_MAX_LISTENERS 8
#define RDP_DYNVC_NAME_MAX 64

struct rdp_dynvc;

/* A complete DVC message for a channel the listener accepted. */
typedef void (*rdp_dynvc_on_data)(struct rdp_dynvc* d, uint32_t channel_id,
                                  const uint8_t* data, size_t len, void* user);
/* Called when the server asks to open name; return 0 to accept, < 0 to refuse. */
typedef int (*rdp_dynvc_on_create)(struct rdp_dynvc* d, uint32_t channel_id,
                                   const char* name, void* user);
typedef void (*rdp_dynvc_on_close)(struct rdp_dynvc* d, uint32_t channel_id, void* user);

/* How the manager sends a DYNVC PDU back (already without CHANNEL_PDU_HEADER;
 * the session wraps it). Return 0 or < 0. */
typedef int (*rdp_dynvc_send_fn)(void* send_user, const uint8_t* pdu, size_t len);

struct rdp_dynvc_listener
{
	char name[RDP_DYNVC_NAME_MAX];
	rdp_dynvc_on_create on_create;
	rdp_dynvc_on_data on_data;
	rdp_dynvc_on_close on_close;
	void* user;
};

struct rdp_dynvc_chan
{
	uint32_t id;
	int open;
	int cb_id_bits;          /* 0/1/2: how the create used ChannelId width */
	char name[RDP_DYNVC_NAME_MAX];
	uint8_t* asm_buf;        /* reassembly of DATA_FIRST + DATA */
	size_t asm_have, asm_total;
	int discard;             /* 1: this message is over RDP_VC_MAX_MESSAGE: read past, not kept */
	struct rdp_dynvc_listener* listener;
};

struct rdp_dynvc
{
	int version;             /* negotiated 1..3, 0 before caps */
	struct rdp_vc_rx vc;     /* CHANNEL_PDU_HEADER reassembly on DRDYNVC */
	struct rdp_dynvc_listener listeners[RDP_DYNVC_MAX_LISTENERS];
	int nlisteners;
	struct rdp_dynvc_chan chans[RDP_DYNVC_MAX_CHANNELS];
	int nchans;
	rdp_dynvc_send_fn send;
	void* send_user;
	uint32_t caps_ok;        /* 1 after CAPS response written */
	uint32_t creates;        /* create requests seen */
	uint32_t data_msgs;      /* complete DVC messages delivered */
	uint32_t dropped;        /* DVC messages dropped for their size (review 2 N5) */
};

void rdp_dynvc_init(struct rdp_dynvc* d, rdp_dynvc_send_fn send, void* send_user);
void rdp_dynvc_free(struct rdp_dynvc* d);

/* Register a listener before the caps exchange. 0, or < 0 when full. */
int rdp_dynvc_listen(struct rdp_dynvc* d, const char* name,
                     rdp_dynvc_on_create on_create, rdp_dynvc_on_data on_data,
                     rdp_dynvc_on_close on_close, void* user);

/* One MCS userData chunk from the DRDYNVC static channel (with header).
 * May call send() for CAPS_RSP / CREATE_RSP / CLOSE. 0, or < 0. */
int rdp_dynvc_input(struct rdp_dynvc* d, const uint8_t* data, size_t len);

/* Build and send a DVC data message on channel_id (fragments as needed). */
int rdp_dynvc_send_data(struct rdp_dynvc* d, uint32_t channel_id,
                        const uint8_t* data, size_t len);

/* Find an open channel by id, or NULL. */
struct rdp_dynvc_chan* rdp_dynvc_find(struct rdp_dynvc* d, uint32_t channel_id);

#endif
