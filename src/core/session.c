/*
 * GFAmigaRDP: the session's connection; see session.h.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "session.h"
#include "amipr.h"
#include "rdpgfx.h"
#include "input.h"

#include <exec/memory.h>
#include <proto/exec.h>
#include <string.h>

#define CONN_BUF 65536   /* what the server sent: a PDU, and the start of the next */
#define TX_BUF 4096      /* what the client sends from here (Confirm Active and the small ones) */

void rdp_conn_init(struct rdp_conn* c)
{
	memset(c, 0, sizeof(*c));
	rdp_transport_init(&c->t);
}

static int need_buf(struct rdp_conn* c)
{
	if (!c->buf)
	{
		c->buf = AllocVec(CONN_BUF + TX_BUF, MEMF_ANY);
		c->tx = c->buf ? c->buf + CONN_BUF : NULL;
		c->have = 0;
	}
	return c->buf ? 0 : AMIPR_ENOMEM;
}

/* Wrap a DYNVC PDU in CHANNEL_PDU_HEADER and send on the drdynvc MCS channel. */
static int dynvc_send(void* user, const uint8_t* pdu, size_t len)
{
	struct rdp_conn* c = (struct rdp_conn*)user;
	uint8_t chunk[8 + 1600];
	size_t sent = 0;
	int rc;

	if (!c || !c->dynvc_on || !c->dynvc_mcs || !c->tx)
		return -1;
	/* CHANNEL_CHUNK_LENGTH is 1600 ([MS-RDPBCGR] 2.2.6): fragment if needed */
	while (sent < len || (len == 0 && sent == 0))
	{
		size_t chunk_len = len - sent;
		uint32_t flags = 0;
		int n;
		if (chunk_len > 1600)
			chunk_len = 1600;
		if (sent == 0)
			flags |= RDP_VC_FLAG_FIRST;
		if (sent + chunk_len >= len)
			flags |= RDP_VC_FLAG_LAST;
		n = rdp_vc_build(chunk, sizeof(chunk), pdu + sent, chunk_len, flags);
		if (n < 0)
			return n;
		/* CHANNEL_PDU_HEADER::length is the whole reassembled length ([MS-RDPBCGR] 2.2.6.1) */
		chunk[0] = (uint8_t)len;
		chunk[1] = (uint8_t)(len >> 8);
		chunk[2] = (uint8_t)(len >> 16);
		chunk[3] = (uint8_t)(len >> 24);
		rc = rdp_mcs_send_data(&c->t, c->tx, TX_BUF, c->mr.user_channel, c->dynvc_mcs,
		                       chunk, (size_t)n);
		if (rc < 0)
			return rc;
		sent += chunk_len;
		if (len == 0)
			break;
	}
	return 0;
}

/* RDPGFX memory: AllocVec(MEMF_ANY) takes Fast RAM first by priority; the
 * buffers are only touched by the CPU, so Chip RAM is never needed */
static void* gfx_alloc(void* user, size_t n)
{
	(void)user;
	return AllocVec((ULONG)(n ? n : 1), MEMF_ANY);
}

static void gfx_free(void* user, void* p)
{
	(void)user;
	FreeVec(p);
}

static int gfx_send(void* user, const uint8_t* data, size_t len)
{
	struct rdp_conn* c = (struct rdp_conn*)user;
	return rdp_dynvc_send_data(&c->dynvc, c->gfx_chan, data, len);
}

static void gfx_drop(struct rdp_conn* c)
{
	if (c->gfx)
	{
		rdp_gfx_free(c->gfx);
		FreeVec(c->gfx);
		c->gfx = NULL;
	}
	c->gfx_open_pending = 0;
	c->gfx_chan = 0;
}

static int on_gfx_create(struct rdp_dynvc* d, uint32_t id, const char* name, void* user)
{
	struct rdp_conn* c = (struct rdp_conn*)user;
	struct rdp_mem mem;
	uint16_t w, h;
	(void)d;
	(void)name;
	if (!c || !c->gfx_enable)
		return 0; /* accept and count only, as before */
	gfx_drop(c);
	c->gfx = (struct rdp_gfx*)AllocVec(sizeof(struct rdp_gfx), MEMF_ANY | MEMF_CLEAR);
	if (!c->gfx)
		return -1;
	mem.alloc = gfx_alloc;
	mem.free = gfx_free;
	mem.user = NULL;
	w = c->demand.width ? c->demand.width : (c->width ? c->width : 640);
	h = c->demand.height ? c->demand.height : (c->height ? c->height : 480);
	if (rdp_gfx_init(c->gfx, &mem, w, h, gfx_send, c) < 0)
	{
		FreeVec(c->gfx);
		c->gfx = NULL;
		return -1;
	}
	c->gfx_chan = id;
	c->gfx->offer_mask = c->gfx_caps_mask;
	/* the caps advertise must follow our DYNVC_CREATE_RSP, which the manager
	 * sends after this returns: rdp_conn_read sends it (see the route) */
	c->gfx_open_pending = 1;
	return 0;
}

static void on_gfx_data(struct rdp_dynvc* d, uint32_t id, const uint8_t* data, size_t len, void* user)
{
	struct rdp_conn* c = (struct rdp_conn*)user;
	(void)d;
	if (!c)
		return;
	c->gfx_msgs++;
	c->gfx_bytes += (uint32_t)len;
	if (c->gfx && id == c->gfx_chan)
		rdp_gfx_input(c->gfx, data, len);   /* errors are counted in gfx->st */
}

static void on_gfx_close(struct rdp_dynvc* d, uint32_t id, void* user)
{
	struct rdp_conn* c = (struct rdp_conn*)user;
	(void)d;
	if (c && c->gfx && id == c->gfx_chan)
		gfx_drop(c);
}

void rdp_conn_bind_dynvc(struct rdp_conn* c)
{
	int i;
	if (!c || c->dynvc_on)
		return;
	for (i = 0; i < c->mr.channel_count; i++)
	{
		if (c->mr.channels[i] && !strcmp(c->mr.channel_names[i], "drdynvc"))
		{
			c->dynvc_mcs = c->mr.channels[i];
			rdp_dynvc_init(&c->dynvc, dynvc_send, c);
			rdp_dynvc_listen(&c->dynvc, "Microsoft::Windows::RDS::Graphics",
			                 on_gfx_create, on_gfx_data, on_gfx_close, c);
			c->dynvc_on = 1;
			return;
		}
	}
}

/* one data PDU on the I/O channel; the payload is small (sync, control, font list) */
static int send_data(struct rdp_conn* c, uint8_t type2, const uint8_t* payload, size_t len)
{
	uint8_t pdu[64];
	struct rdp_stream s;
	rdp_s_init(&s, pdu, sizeof(pdu));
	if (rdp_build_data(&s, c->demand.share_id, c->mr.user_channel, type2, payload, len) < 0)
		return RDP_E_SPACE;
	return rdp_mcs_send_data(&c->t, c->tx, TX_BUF, c->mr.user_channel, c->mr.io_channel, pdu, s.pos);
}

static int confirm(struct rdp_conn* c)
{
	uint8_t pdu[512], p[8];
	struct rdp_stream s;
	uint16_t w = c->demand.width ? c->demand.width : c->width;
	uint16_t h = c->demand.height ? c->demand.height : c->height;
	int rc;
	rdp_s_init(&s, pdu, sizeof(pdu));
	if (rdp_build_confirm_active(&s, &c->demand, c->mr.user_channel, w, h, c->depth ? c->depth : 16,
	                             c->keyboard_layout) < 0)
		return RDP_E_SPACE;
	rc = rdp_mcs_send_data(&c->t, c->tx, TX_BUF, c->mr.user_channel, c->mr.io_channel, pdu, s.pos);
	/* 2.2.1.14 to 2.2.1.18: the client sends these straight after it, in this order */
	if (rc >= 0)
	{
		rdp_payload_sync(p);
		rc = send_data(c, RDP_PDUTYPE2_SYNCHRONIZE, p, 4);
	}
	if (rc >= 0)
	{
		rdp_payload_control(p, RDP_CTRLACTION_COOPERATE);
		rc = send_data(c, RDP_PDUTYPE2_CONTROL, p, 8);
	}
	if (rc >= 0)
	{
		rdp_payload_control(p, RDP_CTRLACTION_REQUEST_CONTROL);
		rc = send_data(c, RDP_PDUTYPE2_CONTROL, p, 8);
	}
	if (rc >= 0)
	{
		rdp_payload_fontlist(p);
		rc = send_data(c, RDP_PDUTYPE2_FONTLIST, p, 8);
	}
	if (rc >= 0)
		c->act = RDP_ACT_CONFIRMED;
	return rc < 0 ? rc : 0;
}

/* the whole PDU of len bytes at the start of c->buf, acted on */
static int handle(struct rdp_conn* c, size_t len)
{
	struct rdp_pdu pdu;
	struct rdp_share sh;
	int rc = rdp_pdu_parse(c->buf, len, &c->mr, &pdu);
	{
		unsigned i = c->ntrace++ % 8;
		const uint8_t* src = pdu.len ? pdu.data : c->buf;   /* the raw TPKT when it carried nothing for the session */
		size_t n = pdu.len ? (pdu.len < 24 ? pdu.len : 24) : 24;
		c->trace[i].fastpath = (uint8_t)pdu.fastpath;
		c->trace[i].choice = pdu.choice;
		c->trace[i].channel = pdu.channel;
		c->trace[i].len = (uint16_t)pdu.len;
		memset(c->trace[i].head, 0, 24);
		memcpy(c->trace[i].head, src, n);
	}
	if (rc == AMIPR_ECLOSED)
		c->act = RDP_ACT_ENDED;
	if (rc < 0)
		return rc;
	c->pdus++;
	if (pdu.fastpath)
		return rdp_fastpath_count(pdu.data, pdu.len, &c->fp) < 0 ? RDP_E_PROTO : 0;
	if (!pdu.len)
		return 0;
	if (pdu.channel != c->mr.io_channel)
	{
		/* DRDYNVC static channel: feed CHANNEL_PDU_HEADER + payload to the manager */
		if (c->dynvc_on && pdu.channel == c->dynvc_mcs)
		{
			rc = rdp_dynvc_input(&c->dynvc, pdu.data, pdu.len);
			if (rc >= 0 && c->gfx_open_pending && c->gfx)
			{
				c->gfx_open_pending = 0;
				rc = rdp_gfx_on_open(c->gfx);   /* RDPGFX_CAPS_ADVERTISE_PDU */
			}
			return rc;
		}
		return 0;   /* message channel and other static VCs: later */
	}
	/* with TLS there is no security header after licensing (5.4.4) */
	if (rdp_share_parse(pdu.data, pdu.len, &sh) < 0)
		return RDP_E_PROTO;
	switch (sh.type)
	{
		case RDP_PDUTYPE_DEMANDACTIVE:
			if (rdp_demand_parse(sh.body, sh.body_len, &c->demand) < 0)
				return RDP_E_PROTO;
			c->demand.source = sh.source;
			c->act = RDP_ACT_DEMAND;
			return confirm(c);
		case RDP_PDUTYPE_DEACTIVATEALL:
			if (!c->demand.ncaps)
				c->deactivated_early = 1;
			c->act = RDP_ACT_DEACTIVATED;
			return 0;
		case RDP_PDUTYPE_SERVER_REDIR:
			/* not advertised (mcs.c), so not followed: a server that sends
			 * one anyway means this session is not where the user is going */
			c->redirected = 1;
			c->act = RDP_ACT_ENDED;
			return RDP_E_REDIRECT;
		case RDP_PDUTYPE_DATA:
			c->last_type2 = sh.type2;
			if (sh.type2 < 32)
				c->other_types |= 1UL << sh.type2;
			switch (sh.type2)
			{
				case RDP_PDUTYPE2_SYNCHRONIZE:
					if (c->act == RDP_ACT_CONFIRMED) c->act = RDP_ACT_SYNC;
					break;
				case RDP_PDUTYPE2_CONTROL:
					/* compare against the activation states only: DEACTIVATED and
					 * ENDED sort after ACTIVE, so a >= CONFIRMED test would bring
					 * a deactivated session back (review L3) */
					if (sh.action == RDP_CTRLACTION_COOPERATE &&
					    (c->act == RDP_ACT_CONFIRMED || c->act == RDP_ACT_SYNC))
						c->act = RDP_ACT_COOPERATE;
					else if (sh.action == RDP_CTRLACTION_GRANTED_CONTROL &&
					         (c->act == RDP_ACT_CONFIRMED || c->act == RDP_ACT_SYNC || c->act == RDP_ACT_COOPERATE))
						c->act = RDP_ACT_GRANTED;
					break;
				case RDP_PDUTYPE2_FONTMAP:
					if (c->act == RDP_ACT_CONFIRMED || c->act == RDP_ACT_SYNC ||
					    c->act == RDP_ACT_COOPERATE || c->act == RDP_ACT_GRANTED)
						c->act = RDP_ACT_ACTIVE;
					break;
				case RDP_PDUTYPE2_SET_ERROR_INFO:
					if (sh.error_info)
						c->error_info = sh.error_info;
					break;
				case RDP_PDUTYPE2_UPDATE:
					c->slow_updates++;
					break;
				case RDP_PDUTYPE2_SAVE_SESSION_INFO:
					/* Auto-reconnect cookie (2.2.10.1); memory only, never a password */
					(void)rdp_arc_from_save_session(&c->arc, sh.body, sh.body_len);
					break;
				default:
					break;
			}
			return 0;
		default:
			return 0;
	}
}

/* The next PDU, if all of it is there, without waiting: what TLS holds
 * already and what the socket has are read until it would block, and a
 * PDU is acted on only when all of it has come; the rest waits in c->buf
 * for the next call (a half PDU never blocks the task; review M4).
 * 1 when one was acted on, 0 when none is complete yet, or < 0. */
static int next_pdu(struct rdp_conn* c)
{
	for (;;)
	{
		int len = rdp_pdu_frame_length(c->buf, c->have, CONN_BUF), n;
		if (len < 0)
			return len;
		if (len > 0 && c->have >= (size_t)len)
		{
			int rc = handle(c, (size_t)len);
			c->have -= (size_t)len;
			if (c->have)
				memmove(c->buf, c->buf + len, c->have);
			return rc < 0 ? rc : 1;
		}
		n = rdp_transport_read_some(&c->t, c->buf + c->have, CONN_BUF - c->have);
		if (n == AMIPR_EAGAIN)
			return 0;
		if (n == AMIPR_ECLOSED)
			c->act = RDP_ACT_ENDED;
		if (n < 0)
			return n;
		c->have += (size_t)n;
	}
}

int rdp_conn_ready(const struct rdp_conn* c)
{
	int len;
	if (!c->live || !c->buf)
		return 0;
	if (rdp_transport_pending((struct rdp_transport*)&c->t) > 0)
		return 1;
	len = rdp_pdu_frame_length(c->buf, c->have, CONN_BUF);
	return len < 0 || (len > 0 && c->have >= (size_t)len);
}

int rdp_conn_activate(struct rdp_conn* c, int max_pdus)
{
	int rc = need_buf(c), n = 0;
	if (rc < 0)
		return rc;
	if (!c->live)
		return AMIPR_ECLOSED;
	rdp_conn_bind_dynvc(c);
	while (n < max_pdus && c->act != RDP_ACT_ACTIVE)
	{
		struct amipr_wait_item it;
		rc = next_pdu(c);
		if (rc < 0)
			return rc;
		if (rc > 0)
		{
			n++;
			continue;
		}
		/* nothing whole yet: wait for the socket, at most the transport's timeout */
		it.fd = c->t.sock;
		it.events = c->t.want ? c->t.want : AMIPR_WAIT_READ;
		it.revents = 0;
		rc = amipr_wait(&it, 1, c->t.timeout_us);
		if (rc == 0)
			return AMIPR_ETIMEOUT;
		if (rc < 0)
			return rc;
	}
	return c->act == RDP_ACT_ACTIVE ? 0 : AMIPR_ETIMEOUT;
}

int rdp_conn_poll(struct rdp_conn* c, int max)
{
	int rc = need_buf(c);
	if (rc < 0)
		return rc;
	(void)rdp_conn_input_flush(c);   /* drain queued fast-path input first */
	rdp_conn_bind_dynvc(c);
	while (c->live && max-- > 0)
	{
		rc = next_pdu(c);
		if (rc <= 0)
			return rc;   /* nothing whole waiting (0), or it ended */
	}
	return 0;
}

void rdp_conn_close(struct rdp_conn* c)
{
	if (c->live && c->act != RDP_ACT_ENDED)
		rdp_mcs_disconnect(&c->t);
	rdp_transport_close(&c->t);
	c->live = 0;
	gfx_drop(c);
	if (c->dynvc_on)
	{
		rdp_dynvc_free(&c->dynvc);
		c->dynvc_on = 0;
		c->dynvc_mcs = 0;
	}
	if (c->buf)
	{
		FreeVec(c->buf);
		c->buf = NULL;
		c->tx = NULL;
		c->have = 0;
	}
}


/* ---- fast-path input (never blocks the UI task) ---- */

int rdp_conn_input_pending(const struct rdp_conn* c)
{
	return c && c->in_q_len > 0;
}

int rdp_conn_input_flush(struct rdp_conn* c)
{
	size_t sent = 0;
	int rc;
	if (!c || !c->live || !c->in_q_len)
		return 0;
	rc = rdp_transport_write_nb(&c->t, c->in_q, c->in_q_len, &sent);
	if (sent > 0)
	{
		if (sent >= c->in_q_len)
			c->in_q_len = 0;
		else
		{
			memmove(c->in_q, c->in_q + sent, c->in_q_len - sent);
			c->in_q_len -= sent;
		}
	}
	if (rc == AMIPR_EAGAIN)
		return 0;
	return rc < 0 ? rc : 0;
}

static int input_queue(struct rdp_conn* c, const uint8_t* pdu, int n)
{
	int rc;
	if (!c || !c->live || n <= 0)
		return RDP_E_ARG;
	if (c->act != RDP_ACT_ACTIVE)
		return 0;   /* not ready for input yet; ignore quietly */
	(void)rdp_conn_input_flush(c);
	if (c->in_q_len + (size_t)n > sizeof(c->in_q))
	{
		c->in_drop++;
		return 0;
	}
	memcpy(c->in_q + c->in_q_len, pdu, (size_t)n);
	c->in_q_len += (size_t)n;
	rc = rdp_conn_input_flush(c);
	if (rc >= 0 && c->in_q_len == 0)
		c->in_sent++;
	return rc;
}

int rdp_conn_input_key(struct rdp_conn* c, uint8_t scancode, int extended, int release)
{
	uint8_t pdu[16];
	int n = rdp_fp_input_key(pdu, sizeof(pdu), scancode, extended, release);
	return n < 0 ? n : input_queue(c, pdu, n);
}

int rdp_conn_input_mouse(struct rdp_conn* c, uint16_t ptr_flags, uint16_t x, uint16_t y)
{
	uint8_t pdu[16];
	int n = rdp_fp_input_mouse(pdu, sizeof(pdu), ptr_flags, x, y);
	return n < 0 ? n : input_queue(c, pdu, n);
}

int rdp_conn_input_cad(struct rdp_conn* c)
{
	uint8_t pdu[64];
	int n = rdp_fp_input_cad(pdu, sizeof(pdu));
	return n < 0 ? n : input_queue(c, pdu, n);
}

const char* rdp_act_name(int act)
{
	switch (act)
	{
		case RDP_ACT_NONE: return "waiting for Demand Active";
		case RDP_ACT_DEMAND: return "Demand Active read";
		case RDP_ACT_CONFIRMED: return "Confirm Active sent";
		case RDP_ACT_SYNC: return "the server's Synchronize read";
		case RDP_ACT_COOPERATE: return "Control Cooperate read";
		case RDP_ACT_GRANTED: return "Control Granted read";
		case RDP_ACT_ACTIVE: return "active (Font Map read)";
		case RDP_ACT_DEACTIVATED: return "deactivated (Deactivate All)";
		case RDP_ACT_ENDED: return "ended by the computer";
		default: return "?";
	}
}

const char* rdp_error_info_text(uint32_t code)
{
	/* [MS-RDPBCGR] 2.2.5.1.1, every code the table lists */
	switch (code)
	{
		case 0x00000001: return "ERRINFO_RPC_INITIATED_DISCONNECT";
		case 0x00000002: return "ERRINFO_RPC_INITIATED_LOGOFF";
		case 0x00000003: return "ERRINFO_IDLE_TIMEOUT";
		case 0x00000004: return "ERRINFO_LOGON_TIMEOUT";
		case 0x00000005: return "ERRINFO_DISCONNECTED_BY_OTHERCONNECTION";
		case 0x00000006: return "ERRINFO_OUT_OF_MEMORY";
		case 0x00000007: return "ERRINFO_SERVER_DENIED_CONNECTION";
		case 0x00000009: return "ERRINFO_SERVER_INSUFFICIENT_PRIVILEGES";
		case 0x0000000A: return "ERRINFO_SERVER_FRESH_CREDENTIALS_REQUIRED";
		case 0x0000000B: return "ERRINFO_RPC_INITIATED_DISCONNECT_BYUSER";
		case 0x0000000C: return "ERRINFO_LOGOFF_BY_USER";
		case 0x0000000F: return "ERRINFO_CLOSE_STACK_ON_DRIVER_NOT_READY";
		case 0x00000010: return "ERRINFO_SERVER_DWM_CRASH";
		case 0x00000011: return "ERRINFO_CLOSE_STACK_ON_DRIVER_FAILURE";
		case 0x00000012: return "ERRINFO_CLOSE_STACK_ON_DRIVER_IFACE_FAILURE";
		case 0x00000017: return "ERRINFO_SERVER_WINLOGON_CRASH";
		case 0x00000018: return "ERRINFO_SERVER_CSRSS_CRASH";
		case 0x00000019: return "ERRINFO_SERVER_SHUTDOWN";
		case 0x0000001A: return "ERRINFO_SERVER_REBOOT";
		case 0x00000100: return "ERRINFO_LICENSE_INTERNAL";
		case 0x00000101: return "ERRINFO_LICENSE_NO_LICENSE_SERVER";
		case 0x00000102: return "ERRINFO_LICENSE_NO_LICENSE";
		case 0x00000103: return "ERRINFO_LICENSE_BAD_CLIENT_MSG";
		case 0x00000104: return "ERRINFO_LICENSE_HWID_DOESNT_MATCH_LICENSE";
		case 0x00000105: return "ERRINFO_LICENSE_BAD_CLIENT_LICENSE";
		case 0x00000106: return "ERRINFO_LICENSE_CANT_FINISH_PROTOCOL";
		case 0x00000107: return "ERRINFO_LICENSE_CLIENT_ENDED_PROTOCOL";
		case 0x00000108: return "ERRINFO_LICENSE_BAD_CLIENT_ENCRYPTION";
		case 0x00000109: return "ERRINFO_LICENSE_CANT_UPGRADE_LICENSE";
		case 0x0000010A: return "ERRINFO_LICENSE_NO_REMOTE_CONNECTIONS";
		case 0x00000400: return "ERRINFO_CB_DESTINATION_NOT_FOUND";
		case 0x00000402: return "ERRINFO_CB_LOADING_DESTINATION";
		case 0x00000404: return "ERRINFO_CB_REDIRECTING_TO_DESTINATION";
		case 0x00000405: return "ERRINFO_CB_SESSION_ONLINE_VM_WAKE";
		case 0x00000406: return "ERRINFO_CB_SESSION_ONLINE_VM_BOOT";
		case 0x00000407: return "ERRINFO_CB_SESSION_ONLINE_VM_NO_DNS";
		case 0x00000408: return "ERRINFO_CB_DESTINATION_POOL_NOT_FREE";
		case 0x00000409: return "ERRINFO_CB_CONNECTION_CANCELLED";
		case 0x00000410: return "ERRINFO_CB_CONNECTION_ERROR_INVALID_SETTINGS";
		case 0x00000411: return "ERRINFO_CB_SESSION_ONLINE_VM_BOOT_TIMEOUT";
		case 0x00000412: return "ERRINFO_CB_SESSION_ONLINE_VM_SESSMON_FAILED";
		case 0x000010C9: return "ERRINFO_UNKNOWNPDUTYPE2";
		case 0x000010CA: return "ERRINFO_UNKNOWNPDUTYPE";
		case 0x000010CB: return "ERRINFO_DATAPDUSEQUENCE";
		case 0x000010CD: return "ERRINFO_CONTROLPDUSEQUENCE";
		case 0x000010CE: return "ERRINFO_INVALIDCONTROLPDUACTION";
		case 0x000010CF: return "ERRINFO_INVALIDINPUTPDUTYPE";
		case 0x000010D0: return "ERRINFO_INVALIDINPUTPDUMOUSE";
		case 0x000010D1: return "ERRINFO_INVALIDREFRESHRECTPDU";
		case 0x000010D2: return "ERRINFO_CREATEUSERDATAFAILED";
		case 0x000010D3: return "ERRINFO_CONNECTFAILED";
		case 0x000010D4: return "ERRINFO_CONFIRMACTIVEWRONGSHAREID";
		case 0x000010D5: return "ERRINFO_CONFIRMACTIVEWRONGORIGINATOR";
		case 0x000010DA: return "ERRINFO_PERSISTENTKEYPDUBADLENGTH";
		case 0x000010DB: return "ERRINFO_PERSISTENTKEYPDUILLEGALFIRST";
		case 0x000010DC: return "ERRINFO_PERSISTENTKEYPDUTOOMANYTOTALKEYS";
		case 0x000010DD: return "ERRINFO_PERSISTENTKEYPDUTOOMANYCACHEKEYS";
		case 0x000010DE: return "ERRINFO_INPUTPDUBADLENGTH";
		case 0x000010DF: return "ERRINFO_BITMAPCACHEERRORPDUBADLENGTH";
		case 0x000010E0: return "ERRINFO_SECURITYDATATOOSHORT";
		case 0x000010E1: return "ERRINFO_VCHANNELDATATOOSHORT";
		case 0x000010E2: return "ERRINFO_SHAREDATATOOSHORT";
		case 0x000010E3: return "ERRINFO_BADSUPRESSOUTPUTPDU";
		case 0x000010E5: return "ERRINFO_CONFIRMACTIVEPDUTOOSHORT";
		case 0x000010E7: return "ERRINFO_CAPABILITYSETTOOSMALL";
		case 0x000010E8: return "ERRINFO_CAPABILITYSETTOOLARGE";
		case 0x000010E9: return "ERRINFO_NOCURSORCACHE";
		case 0x000010EA: return "ERRINFO_BADCAPABILITIES";
		case 0x000010EC: return "ERRINFO_VIRTUALCHANNELDECOMPRESSIONERR";
		case 0x000010ED: return "ERRINFO_INVALIDVCCOMPRESSIONTYPE";
		case 0x000010EF: return "ERRINFO_INVALIDCHANNELID";
		case 0x000010F0: return "ERRINFO_VCHANNELSTOOMANY";
		case 0x000010F3: return "ERRINFO_REMOTEAPPSNOTENABLED";
		case 0x000010F4: return "ERRINFO_CACHECAPNOTSET";
		case 0x000010F5: return "ERRINFO_BITMAPCACHEERRORPDUBADLENGTH2";
		case 0x000010F6: return "ERRINFO_OFFSCRCACHEERRORPDUBADLENGTH";
		case 0x000010F7: return "ERRINFO_DNGCACHEERRORPDUBADLENGTH";
		case 0x000010F8: return "ERRINFO_GDIPLUSPDUBADLENGTH";
		case 0x00001111: return "ERRINFO_SECURITYDATATOOSHORT2";
		case 0x00001112: return "ERRINFO_SECURITYDATATOOSHORT3";
		case 0x00001113: return "ERRINFO_SECURITYDATATOOSHORT4";
		case 0x00001114: return "ERRINFO_SECURITYDATATOOSHORT5";
		case 0x00001115: return "ERRINFO_SECURITYDATATOOSHORT6";
		case 0x00001116: return "ERRINFO_SECURITYDATATOOSHORT7";
		case 0x00001117: return "ERRINFO_SECURITYDATATOOSHORT8";
		case 0x00001118: return "ERRINFO_SECURITYDATATOOSHORT9";
		case 0x00001119: return "ERRINFO_SECURITYDATATOOSHORT10";
		case 0x0000111A: return "ERRINFO_SECURITYDATATOOSHORT11";
		case 0x0000111B: return "ERRINFO_SECURITYDATATOOSHORT12";
		case 0x0000111C: return "ERRINFO_SECURITYDATATOOSHORT13";
		case 0x0000111D: return "ERRINFO_SECURITYDATATOOSHORT14";
		case 0x0000111E: return "ERRINFO_SECURITYDATATOOSHORT15";
		case 0x0000111F: return "ERRINFO_SECURITYDATATOOSHORT16";
		case 0x00001120: return "ERRINFO_SECURITYDATATOOSHORT17";
		case 0x00001121: return "ERRINFO_SECURITYDATATOOSHORT18";
		case 0x00001122: return "ERRINFO_SECURITYDATATOOSHORT19";
		case 0x00001123: return "ERRINFO_SECURITYDATATOOSHORT20";
		case 0x00001124: return "ERRINFO_SECURITYDATATOOSHORT21";
		case 0x00001125: return "ERRINFO_SECURITYDATATOOSHORT22";
		case 0x00001126: return "ERRINFO_SECURITYDATATOOSHORT23";
		case 0x00001129: return "ERRINFO_BADMONITORDATA";
		case 0x0000112A: return "ERRINFO_VCDECOMPRESSEDREASSEMBLEFAILED";
		case 0x0000112B: return "ERRINFO_VCDATATOOLONG";
		case 0x0000112C: return "ERRINFO_BAD_FRAME_ACK_DATA";
		case 0x0000112D: return "ERRINFO_GRAPHICSMODENOTSUPPORTED";
		case 0x0000112E: return "ERRINFO_GRAPHICSSUBSYSTEMRESETFAILED";
		case 0x0000112F: return "ERRINFO_GRAPHICSSUBSYSTEMFAILED";
		case 0x00001130: return "ERRINFO_TIMEZONEKEYNAMELENGTHTOOSHORT";
		case 0x00001131: return "ERRINFO_TIMEZONEKEYNAMELENGTHTOOLONG";
		case 0x00001132: return "ERRINFO_DYNAMICDSTDISABLEDFIELDMISSING";
		case 0x00001133: return "ERRINFO_VCDECODINGERROR";
		case 0x00001134: return "ERRINFO_VIRTUALDESKTOPTOOLARGE";
		case 0x00001135: return "ERRINFO_MONITORGEOMETRYVALIDATIONFAILED";
		case 0x00001136: return "ERRINFO_INVALIDMONITORCOUNT";
		case 0x00001191: return "ERRINFO_UPDATESESSIONKEYFAILED";
		case 0x00001192: return "ERRINFO_DECRYPTFAILED";
		case 0x00001193: return "ERRINFO_ENCRYPTFAILED";
		case 0x00001194: return "ERRINFO_ENCPKGMISMATCH";
		case 0x00001195: return "ERRINFO_DECRYPTFAILED2";
		default: return NULL;
	}
}
