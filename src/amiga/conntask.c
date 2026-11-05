/*
 * GFAmigaRDP: the connection task; see conntask.h.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "conntask.h"
#include "amipr.h"
#include "connect.h"
#include "nla.h"
#include "mcs.h"
#include "x509.h"
#include "session.h"
#include "knownhosts.h"
#include "transport.h"

#include <exec/memory.h>
#include <exec/tasks.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>

#define CT_STACK 65536
#define CT_NAME "GFAmigaRDP connection"

struct ct_priv
{
	struct ct_handle* h;
	struct MsgPort* port;
	volatile int cancel;
	volatile int cert_reply;   /* -1 waiting, 0 refuse, 1 trust */
	volatile int cert_waiting;
	char password[512];
	struct ct_msg params;      /* last CONNECT copy (no password after wipe) */
};

static struct ct_priv* s_priv; /* the running task's; one connection task */

static struct ct_msg* alloc_msg(uint16_t type)
{
	struct ct_msg* m = AllocVec(sizeof(*m), MEMF_CLEAR | MEMF_PUBLIC);
	if (!m)
		return NULL;
	m->msg.mn_Node.ln_Type = NT_MESSAGE;
	m->msg.mn_Length = (UWORD)sizeof(*m);
	m->type = type;
	return m;
}

static int post_ui(struct ct_handle* h, struct ct_msg* m)
{
	if (!h || !h->ui_port || !m)
	{
		if (m) FreeVec(m);
		return -1;
	}
	PutMsg(h->ui_port, &m->msg);
	return 0;
}

static int evt_state(struct ct_handle* h, const char* text)
{
	struct ct_msg* m = alloc_msg(CT_EVT_STATE);
	if (!m) return -1;
	if (text) strncpy(m->text, text, sizeof(m->text) - 1);
	return post_ui(h, m);
}

static int evt_error(struct ct_handle* h, int rc, const char* text)
{
	struct ct_msg* m = alloc_msg(CT_EVT_ERROR);
	if (!m) return -1;
	m->arg = (int16_t)rc;
	if (text) strncpy(m->text, text, sizeof(m->text) - 1);
	return post_ui(h, m);
}

static void wipe(volatile char* p, size_t n)
{
	while (n--) *p++ = 0;
}

/* Wait for socket and/or our command port. Returns:
 *  1 = socket ready (or timeout 0 / pending)
 *  0 = timeout
 * <0 = cancel / error
 * Also drains CT_CMD_CANCEL / CERT / QUIT / DISCONNECT from the port. */
static int wait_net(struct ct_priv* p, struct rdp_transport* t, int64_t timeout_us)
{
	struct amipr_wait_item it;
	uint32_t sigs;
	ULONG portsig = 1UL << p->port->mp_SigBit;
	int rc;

	for (;;)
	{
		struct ct_msg* m;
		while ((m = (struct ct_msg*)GetMsg(p->port)) != NULL)
		{
			uint16_t ty = m->type;
			int16_t arg = m->arg;
			if (ty == CT_CMD_CANCEL || ty == CT_CMD_DISCONNECT || ty == CT_CMD_QUIT)
				p->cancel = 1;
			if (ty == CT_CMD_CERT && p->cert_waiting)
			{
				p->cert_reply = arg ? 1 : 0;
				p->cert_waiting = 0;
			}
			if (ty == CT_CMD_QUIT)
			{
				FreeVec(m);
				return AMIPR_ECANCEL;
			}
			FreeVec(m);
			if (p->cancel)
				return AMIPR_ECANCEL;
		}
		if (p->cancel)
			return AMIPR_ECANCEL;

		sigs = portsig | SIGBREAKF_CTRL_C;
		if (t && t->sock >= 0)
		{
			it.fd = t->sock;
			it.events = t->want ? t->want : AMIPR_WAIT_READ;
			it.revents = 0;
			rc = amipr_wait_signals(&it, 1, timeout_us, &sigs);
		}
		else
		{
			it.fd = -1;
			rc = amipr_wait_signals(NULL, 0, timeout_us, &sigs);
		}
		if (sigs & SIGBREAKF_CTRL_C)
		{
			p->cancel = 1;
			return AMIPR_ECANCEL;
		}
		if (sigs & portsig)
			continue; /* drain above */
		if (rc < 0)
			return rc;
		if (rc == 0)
			return 0; /* timeout */
		return 1;     /* socket */
	}
}

/* Abort hook for amipr_wait inside transport/nla/mcs: wake on our port. */
static int abort_cb(void* user, uint32_t sigs)
{
	struct ct_priv* p = user;
	struct ct_msg* m;
	(void)sigs;
	while ((m = (struct ct_msg*)GetMsg(p->port)) != NULL)
	{
		uint16_t ty = m->type;
		int16_t arg = m->arg;
		if (ty == CT_CMD_CANCEL || ty == CT_CMD_DISCONNECT || ty == CT_CMD_QUIT)
			p->cancel = 1;
		if (ty == CT_CMD_CERT && p->cert_waiting)
		{
			p->cert_reply = arg ? 1 : 0;
			p->cert_waiting = 0;
		}
		FreeVec(m);
	}
	return p->cancel;
}

static int ask_cert(struct ct_priv* p, struct rdp_transport* t, const char* host, uint16_t port, int changed)
{
	struct ct_msg* m = alloc_msg(CT_EVT_CERT);
	uint8_t fp[32];
	char text[96], cn[128];
	const uint8_t* der;
	size_t len;
	struct rdp_x509 x;
	uint64_t deadline;
	(void)port;

	if (!m) return -1;
	cn[0] = 0;
	if (amipr_tls_peer_fingerprint(t->tls, fp) != 0)
	{
		FreeVec(m);
		return -1;
	}
	memcpy(m->fp, fp, 32);
	m->changed = changed;
	if (amipr_tls_peer_certificate(t->tls, &der, &len) == 0 && rdp_x509_parse(&x, der, len) == 0)
		rdp_x509_common_name(&x, cn, sizeof(cn));
	rdp_fingerprint_text(fp, text);
	snprintf(m->text, sizeof(m->text), "%s\n%s", cn[0] ? cn : "(no name)", text);
	strncpy(m->host, host, sizeof(m->host) - 1);
	p->cert_reply = -1;
	p->cert_waiting = 1;
	if (post_ui(p->h, m) < 0)
	{
		p->cert_waiting = 0;
		return -1;
	}
	/* wait up to 10 minutes for the user's answer */
	deadline = amipr_monotonic_us() + 600000000ULL;
	while (p->cert_waiting && !p->cancel)
	{
		int64_t left = (int64_t)(deadline - amipr_monotonic_us());
		if (left <= 0)
			break;
		if (wait_net(p, NULL, left > 1000000 ? 1000000 : left) == AMIPR_ECANCEL)
			break;
	}
	p->cert_waiting = 0;
	return p->cert_reply == 1 ? 1 : 0;
}

static void do_connect(struct ct_priv* p, struct ct_msg* req)
{
	struct rdp_settings s;
	struct rdp_conn* conn;
	struct rdp_transport* t;
	if (!p->h || !p->h->conn)
	{
		evt_error(p->h, RDP_E_ARG, "Out of memory.");
		return;
	}
	conn = p->h->conn;
	t = &conn->t;
	struct rdp_x224_cc cc;
	char line[320];
	int rc, tries = 0;
	struct ct_handle* h = p->h;

	p->cancel = 0;
	h->busy = 1;
	memcpy(&p->params, req, sizeof(p->params));
	wipe(p->params.password, sizeof(p->params.password));
	strncpy(p->password, req->password, sizeof(p->password) - 1);
	wipe(req->password, sizeof(req->password));

	rdp_settings_default(&s);
	s.host = p->params.host;
	s.port = p->params.port ? p->params.port : 3389;
	s.user = p->params.user;
	s.domain = p->params.domain;
	s.protocols = p->params.protocols;
	s.width = p->params.width;
	s.height = p->params.height;
	s.depth = p->params.depth;
	if (p->params.early_caps) s.early_caps = p->params.early_caps;
	if (p->params.color_depths) s.color_depths = p->params.color_depths;
	if (p->params.channels[0]) s.channels = p->params.channels;

	amipr_set_abort(1UL << p->port->mp_SigBit, abort_cb, p);

again:
	rdp_conn_close(conn);
	rdp_conn_init(conn);
	t = &conn->t;

	snprintf(line, sizeof(line), "Connecting to %s...", p->params.host);
	evt_state(h, line);

	rc = rdp_transport_connect(t, p->params.host, s.port, s.timeout_us);
	if (p->cancel) { rc = AMIPR_ECANCEL; goto fail; }
	if (rc == 0)
		rc = rdp_negotiate(t, &s, &cc);
	if (p->cancel) { rc = AMIPR_ECANCEL; goto fail; }
	if (rc == 0 && cc.has_neg && cc.neg_type == RDP_NEG_FAILURE)
	{
		evt_error(h, rc, rdp_negfail_text(cc.failure));
		goto fail;
	}
	if (rc == 0 && cc.selected == RDP_PROTOCOL_RDP)
	{
		evt_error(h, rc, "The computer offers only standard RDP security, which this version does not do.");
		goto fail;
	}
	if (rc == 0 && cc.has_neg && cc.neg_type == RDP_NEG_RSP &&
	    !(cc.selected & (RDP_PROTOCOL_SSL | RDP_PROTOCOL_HYBRID | RDP_PROTOCOL_HYBRID_EX)))
	{
		evt_error(h, rc, "The computer chose a security protocol this version does not use.");
		goto fail;
	}
	if (rc == 0)
	{
		evt_state(h, "Making a secure connection...");
		t->have_pin = rdp_known_host_get(p->params.host, s.port, t->pin);
		rc = rdp_transport_start_tls(t);
		if (rc == AMIPR_ECERT && !tries++ && !p->cancel)
		{
			evt_state(h, "The computer's certificate needs your answer.");
			if (ask_cert(p, t, p->params.host, s.port, t->have_pin))
			{
				uint8_t fp[32];
				if (amipr_tls_peer_fingerprint(t->tls, fp) == 0)
					rdp_known_host_set(p->params.host, s.port, fp);
				evt_state(h, "Connecting again with the trusted certificate...");
				goto again;
			}
			evt_error(h, AMIPR_ECERT, "Not connected: the certificate was not trusted.");
			goto fail;
		}
	}
	if (rc != 0)
	{
		if (rc == AMIPR_ECANCEL || p->cancel)
			evt_error(h, AMIPR_ECANCEL, "Cancelled.");
		else
		{
			snprintf(line, sizeof(line), "%s", rdp_error_text(rc));
			evt_error(h, rc, line);
		}
		goto fail;
	}
	if (cc.selected & (RDP_PROTOCOL_HYBRID | RDP_PROTOCOL_HYBRID_EX))
	{
		const uint8_t* der;
		size_t len;
		struct rdp_x509 x;
		struct rdp_nla_result nr;
		if (!p->params.user[0])
		{
			evt_error(h, RDP_E_NLA, "This computer needs a user name and password first (NLA).");
			goto fail;
		}
		if (amipr_tls_peer_certificate(t->tls, &der, &len) != 0 || rdp_x509_parse(&x, der, len) != 0)
		{
			evt_error(h, rc, "The computer's certificate could not be read.");
			goto fail;
		}
		evt_state(h, "Logging on...");
		{
			uint8_t cb[64];
			int cbl = rdp_x509_channel_binding(&x, cb, sizeof(cb), NULL);
			rc = rdp_nla(t, &s, p->password, x.pubkey, x.pubkey_len,
			             cbl > 0 ? cb : NULL, cbl > 0 ? (size_t)cbl : 0,
			             (cc.selected & RDP_PROTOCOL_HYBRID_EX) != 0, &nr);
		}
		if (rc != 0)
		{
			snprintf(line, sizeof(line), "Logon failed (%s).", rdp_error_text(rc));
			evt_error(h, rc, line);
			goto fail;
		}
	}
	{
		struct rdp_mcs_result* mr = &conn->mr;
		evt_state(h, "Joining the session's channels...");
		rc = rdp_mcs_connect(t, &s, cc.selected, p->password, mr);
		wipe(p->password, sizeof(p->password));
		if (rc != 0)
		{
			if (p->cancel)
				evt_error(h, AMIPR_ECANCEL, "Cancelled.");
			else
			{
				snprintf(line, sizeof(line), "Stopped after %s: %s",
				         rdp_mcs_stage_name(mr->stage), rdp_error_text(rc));
				evt_error(h, rc, line);
			}
			goto fail;
		}
		{
			struct ct_msg* ready = alloc_msg(CT_EVT_READY);
			if (ready)
			{
				snprintf(ready->security, sizeof(ready->security), "%s, %s %s",
				         cc.selected == RDP_PROTOCOL_SSL ? "TLS" :
				         (cc.selected & RDP_PROTOCOL_HYBRID_EX) ? "NLA (EX)" : "NLA",
				         amipr_tls_protocol(t->tls) == 0x0304 ? "TLS 1.3" : "TLS 1.2",
				         amipr_tls_cipher(t->tls) ? amipr_tls_cipher(t->tls) : "");
				strncpy(ready->host, p->params.host, sizeof(ready->host) - 1);
				strncpy(ready->user, p->params.user, sizeof(ready->user) - 1);
				strncpy(ready->domain, p->params.domain, sizeof(ready->domain) - 1);
				ready->width = p->params.width;
				ready->height = p->params.height;
				ready->depth = p->params.depth;
				post_ui(h, ready);
			}
		}
	}
	conn->live = 1;
	conn->selected = cc.selected;
	conn->width = p->params.width;
	conn->height = p->params.height;
	conn->depth = p->params.depth;
	conn->keyboard_layout = s.keyboard_layout;
	/* Hand the live connection to the UI/session window; do not close it. */
	amipr_set_abort(0, NULL, NULL);
	wipe(p->password, sizeof(p->password));
	h->busy = 0;
	return;

fail:
	amipr_set_abort(0, NULL, NULL);
	wipe(p->password, sizeof(p->password));
	rdp_conn_close(conn);
	h->busy = 0;
}

static void ct_entry(void)
{
	struct ct_priv* p = s_priv;
	struct ct_handle* h;
	struct MsgPort* port;

	if (!p) return;
	h = p->h;
	port = CreateMsgPort();
	if (!port)
	{
		h->running = 0;
		return;
	}
	p->port = port;
	h->conn_port = port;
	amipr_init();
	/* own bsdsocket base for this task (bsdsocket is per-task) */
	amipr_net_open();

	h->running = 1;
	for (;;)
	{
		struct ct_msg* m;
		ULONG sig = Wait(1UL << port->mp_SigBit | SIGBREAKF_CTRL_C);
		if (sig & SIGBREAKF_CTRL_C)
			break;
		while ((m = (struct ct_msg*)GetMsg(port)) != NULL)
		{
			uint16_t ty = m->type;
			if (ty == CT_CMD_QUIT)
			{
				FreeVec(m);
				goto done;
			}
			if (ty == CT_CMD_CONNECT && !h->busy)
				do_connect(p, m);
			/* CANCEL/CERT/DISCONNECT while idle: ignore */
			FreeVec(m);
		}
	}
done:
	/* If a connect was in flight, drop it. A handed-off live conn is the UI's. */
	if (h->busy && h->conn)
		rdp_conn_close(h->conn);
	amipr_set_abort(0, NULL, NULL);
	amipr_net_close();
	amipr_thread_exit();
	{
		struct ct_msg* q = alloc_msg(CT_EVT_QUITTED);
		if (q) post_ui(h, q);
	}
	h->conn_port = NULL;
	h->running = 0;
	h->busy = 0;
	DeleteMsgPort(port);
	p->port = NULL;
}

int ct_init(struct ct_handle* h)
{
	memset(h, 0, sizeof(*h));
	h->ui_port = CreateMsgPort();
	return h->ui_port ? 0 : -1;
}

int ct_ensure(struct ct_handle* h)
{
	static struct ct_priv priv;
	if (!h || !h->ui_port)
		return -1;
	if (h->running && h->proc)
		return 0;
	memset(&priv, 0, sizeof(priv));
	priv.h = h;
	s_priv = &priv;
	Forbid();
	h->proc = CreateNewProcTags(
		NP_Entry, (ULONG)ct_entry,
		NP_Name, (ULONG)CT_NAME,
		NP_StackSize, CT_STACK,
		NP_Priority, 0,
		NP_WindowPtr, (ULONG)-1,
		NP_CopyVars, FALSE,
		TAG_END);
	Permit();
	if (!h->proc)
	{
		s_priv = NULL;
		return -1;
	}
	/* wait until the task has created its port (or failed) */
	{
		int i;
		for (i = 0; i < 200 && !h->running && h->proc; i++)
			amipr_sleep_us(10000);
	}
	return h->running ? 0 : -1;
}

void ct_shutdown(struct ct_handle* h)
{
	struct ct_msg* m;
	uint64_t deadline;
	if (!h) return;
	if (h->running && h->conn_port)
	{
		m = alloc_msg(CT_CMD_QUIT);
		if (m)
		{
			m->msg.mn_ReplyPort = NULL;
			PutMsg(h->conn_port, &m->msg);
		}
		deadline = amipr_monotonic_us() + 5000000ULL;
		while (h->running && amipr_monotonic_us() < deadline)
		{
			struct ct_msg* e;
			uint32_t sigs = 1UL << h->ui_port->mp_SigBit;
			amipr_wait_signals(NULL, 0, 100000, &sigs);
			while ((e = ct_get(h)) != NULL)
			{
				if (e->type == CT_EVT_QUITTED)
				{
					FreeVec(e);
					goto cleared;
				}
				FreeVec(e);
			}
		}
	}
cleared:
	while ((m = ct_get(h)) != NULL)
		FreeVec(m);
	if (h->ui_port)
	{
		DeleteMsgPort(h->ui_port);
		h->ui_port = NULL;
	}
	h->proc = NULL;
	h->running = 0;
	s_priv = NULL;
}

static int post_cmd(struct ct_handle* h, struct ct_msg* m)
{
	if (!h || !h->conn_port || !m)
	{
		if (m) FreeVec(m);
		return -1;
	}
	PutMsg(h->conn_port, &m->msg);
	return 0;
}

int ct_connect(struct ct_handle* h, const struct ct_msg* params)
{
	struct ct_msg* m;
	if (ct_ensure(h) < 0)
		return -1;
	m = alloc_msg(CT_CMD_CONNECT);
	if (!m) return -1;
	/* copy connect fields; password included (task wipes after use) */
	strncpy(m->host, params->host, sizeof(m->host) - 1);
	strncpy(m->user, params->user, sizeof(m->user) - 1);
	strncpy(m->domain, params->domain, sizeof(m->domain) - 1);
	strncpy(m->password, params->password, sizeof(m->password) - 1);
	m->port = params->port;
	m->width = params->width;
	m->height = params->height;
	m->depth = params->depth;
	m->protocols = params->protocols;
	m->early_caps = params->early_caps;
	m->color_depths = params->color_depths;
	strncpy(m->channels, params->channels, sizeof(m->channels) - 1);
	return post_cmd(h, m);
}

int ct_cancel(struct ct_handle* h)
{
	struct ct_msg* m = alloc_msg(CT_CMD_CANCEL);
	return post_cmd(h, m);
}

int ct_cert_reply(struct ct_handle* h, int trust)
{
	struct ct_msg* m = alloc_msg(CT_CMD_CERT);
	if (!m) return -1;
	m->arg = trust ? 1 : 0;
	return post_cmd(h, m);
}

int ct_disconnect(struct ct_handle* h)
{
	struct ct_msg* m = alloc_msg(CT_CMD_DISCONNECT);
	return post_cmd(h, m);
}

struct ct_msg* ct_get(struct ct_handle* h)
{
	if (!h || !h->ui_port)
		return NULL;
	return (struct ct_msg*)GetMsg(h->ui_port);
}
