/*
 * GFAmigaRDP: the connection task. Owns the socket, TLS, NLA, MCS and the
 * session state machine up to licensing; talks to the UI only through exec
 * message ports (docs/DESIGN.md, build plan 5 Nov 2026). The UI never blocks
 * on the network: Cancel, the certificate dialog and the status line stay live.
 * After CT_EVT_READY the live rdp_conn is left for the session window.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_CONNTASK_H
#define RDP_CONNTASK_H

#include <exec/types.h>
#include <exec/ports.h>
#include <stdint.h>

struct rdp_conn;

/* UI -> connection */
#define CT_CMD_CONNECT    1
#define CT_CMD_CANCEL     2
#define CT_CMD_CERT       3   /* arg: 1 trust, 0 refuse */
#define CT_CMD_DISCONNECT 4
#define CT_CMD_QUIT       5

/* connection -> UI */
#define CT_EVT_STATE      10  /* text: status line */
#define CT_EVT_CERT       11  /* text + fp[32] + changed */
#define CT_EVT_ERROR      12  /* text; connection failed / ended */
#define CT_EVT_READY      13  /* licensing done; session window may open */
#define CT_EVT_ACTIVE     14  /* Font Map; session is up (reserved) */
#define CT_EVT_ENDED      15  /* server or we closed (reserved) */
#define CT_EVT_QUITTED    16  /* task is about to exit */

struct ct_msg
{
	struct Message msg;
	uint16_t type;
	int16_t arg;             /* CERT: 1/0; ERROR: rc */
	char text[320];
	uint8_t fp[32];
	int changed;
	char host[256];
	char user[256];
	char domain[256];
	char password[512];
	uint16_t port;
	uint16_t width, height;
	uint8_t depth;
	uint32_t protocols;
	uint16_t early_caps;
	uint16_t color_depths;
	char channels[64];
	uint8_t gfx;              /* 1: decode the Graphics Pipeline (rdp_conn.gfx_enable) */
	char security[128];
};

struct ct_handle
{
	struct MsgPort* ui_port;
	struct MsgPort* conn_port;
	struct Process* proc;
	struct rdp_conn* conn;   /* the app's; filled up to licensing, then handed on */
	volatile int running;
	volatile int busy;
};

int ct_init(struct ct_handle* h);
int ct_ensure(struct ct_handle* h);
void ct_shutdown(struct ct_handle* h);
int ct_connect(struct ct_handle* h, const struct ct_msg* params);
int ct_cancel(struct ct_handle* h);
int ct_cert_reply(struct ct_handle* h, int trust);
int ct_disconnect(struct ct_handle* h);
struct ct_msg* ct_get(struct ct_handle* h);

#endif
