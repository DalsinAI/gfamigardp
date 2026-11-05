/*
 * GFAmigaRDP: a connection that outlives the connection window: the
 * transport (TCP, TLS), what MCS set up (the user and I/O channels), and
 * the session's Capabilities Exchange and Connection Finalization
 * ([MS-RDPBCGR] 1.3.1.1 phases 9 and 10). The connection window fills it
 * up to licensing and hands it on; the session window owns it from then
 * and closes it (rdp_conn_close) when it closes.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_SESSION_H
#define RDP_SESSION_H

#include "connect.h"
#include "mcs.h"
#include "pdu.h"

/* how far the session got */
enum
{
	RDP_ACT_NONE,           /* licensing done, waiting for Demand Active */
	RDP_ACT_DEMAND,         /* Demand Active read */
	RDP_ACT_CONFIRMED,      /* Confirm Active, Synchronize, Control x2 and Font List written */
	RDP_ACT_SYNC,           /* the server's Synchronize read */
	RDP_ACT_COOPERATE,      /* its Control Cooperate read */
	RDP_ACT_GRANTED,        /* its Control Granted Control read */
	RDP_ACT_ACTIVE,         /* its Font Map read: the session is up */
	RDP_ACT_DEACTIVATED,    /* Deactivate All: a Demand Active comes again (a resize, a reconnection) */
	RDP_ACT_ENDED           /* the server ended it (see disconnect_reason, error_info) */
};

struct rdp_conn
{
	int live;                    /* 1: t is connected and this owns it */
	struct rdp_transport t;
	uint32_t selected;           /* the security protocol the server chose */
	struct rdp_mcs_result mr;
	uint16_t width, height, depth;   /* what the client asked for */
	int act;                     /* RDP_ACT_* */
	struct rdp_demand demand;
	uint32_t error_info;         /* the last Set Error Info, 0 none */
	uint32_t pdus;               /* PDUs read since the window took it */
	uint32_t slow_updates;       /* slow-path Update PDUs */
	uint32_t other_types;        /* bit n: a slow-path data PDU with pduType2 n (n < 32) came */
	uint8_t last_type2;
	uint8_t deactivated_early;   /* 1: Deactivate All came before any Demand Active (1.3.1.3: the
	                                server means to drop the connection, not re-exchange capabilities) */
	uint8_t redirected;          /* 1: an Enhanced Security Server Redirection PDU came (2.2.13.3.1):
	                                the session ends with RDP_E_REDIRECT; it is not followed, nor
	                                advertised (TS_UD_CS_CLUSTER) */
	struct rdp_fp_counts fp;
	uint8_t* buf;                /* 64 KiB: what came, a PDU and the start of the next */
	size_t have;                 /* bytes of it read and not acted on yet */
	uint8_t* tx;                 /* 4 KiB after it, for what the client sends */
	/* the last few PDUs read, for RDPProbe's trace */
	struct
	{
		uint8_t fastpath, choice;
		uint16_t channel;
		uint16_t len;
		uint8_t head[24];
	} trace[8];
	unsigned ntrace;
};

void rdp_conn_init(struct rdp_conn* c);
/* Reads until Demand Active, answers it and the finalization PDUs, and
 * reads until the Font Map, at most each transport timeout per PDU and
 * max_pdus PDUs in all. 0 when the session is active; RDP_E_PROTO, a
 * transport error, or AMIPR_ECLOSED when the server ended it (c->act
 * RDP_ACT_ENDED, mr.disconnect_reason, error_info). */
int rdp_conn_activate(struct rdp_conn* c, int max_pdus);
/* Reads what the server has sent, without waiting: at most max PDUs, each
 * acted on only once all of it has come (a half PDU stays for the next
 * call). 0, or < 0 as above when it ended. Activation goes on here too:
 * a window that polls sees c->act move to RDP_ACT_ACTIVE. */
int rdp_conn_poll(struct rdp_conn* c, int max);
/* 1 when a poll has something without the socket being readable (TLS
 * holds decrypted bytes, or a whole PDU waits in buf): wait with timeout 0 */
int rdp_conn_ready(const struct rdp_conn* c);
/* The Disconnect Provider Ultimatum when it is live, then closes it all. */
void rdp_conn_close(struct rdp_conn* c);

const char* rdp_act_name(int act);
/* [MS-RDPBCGR] 2.2.5.1.1 errorInfo, in words, or NULL */
const char* rdp_error_info_text(uint32_t code);

#endif
