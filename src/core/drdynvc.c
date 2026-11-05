/*
 * GFAmigaRDP: Dynamic Virtual Channel manager; see drdynvc.h.
 * Written from [MS-RDPEDYC] only (CLEAN-ROOM.md).
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "drdynvc.h"
#include "connect.h"

#include <stdlib.h>
#include <string.h>

void rdp_dynvc_init(struct rdp_dynvc* d, rdp_dynvc_send_fn send, void* send_user)
{
	memset(d, 0, sizeof(*d));
	rdp_vc_rx_init(&d->vc);
	d->send = send;
	d->send_user = send_user;
}

void rdp_dynvc_free(struct rdp_dynvc* d)
{
	int i;
	rdp_vc_rx_free(&d->vc);
	for (i = 0; i < d->nchans; i++)
	{
		free(d->chans[i].asm_buf);
		d->chans[i].asm_buf = NULL;
	}
	d->nchans = 0;
}

int rdp_dynvc_listen(struct rdp_dynvc* d, const char* name,
                     rdp_dynvc_on_create on_create, rdp_dynvc_on_data on_data,
                     rdp_dynvc_on_close on_close, void* user)
{
	struct rdp_dynvc_listener* L;
	if (!d || !name || !*name || d->nlisteners >= RDP_DYNVC_MAX_LISTENERS)
		return -1;
	if (strlen(name) >= RDP_DYNVC_NAME_MAX)
		return -1;
	L = &d->listeners[d->nlisteners++];
	memset(L, 0, sizeof(*L));
	strncpy(L->name, name, RDP_DYNVC_NAME_MAX - 1);
	L->on_create = on_create;
	L->on_data = on_data;
	L->on_close = on_close;
	L->user = user;
	return 0;
}

struct rdp_dynvc_chan* rdp_dynvc_find(struct rdp_dynvc* d, uint32_t channel_id)
{
	int i;
	for (i = 0; i < d->nchans; i++)
		if (d->chans[i].open && d->chans[i].id == channel_id)
			return &d->chans[i];
	return NULL;
}

static struct rdp_dynvc_listener* find_listener(struct rdp_dynvc* d, const char* name)
{
	int i;
	for (i = 0; i < d->nlisteners; i++)
		if (!strcmp(d->listeners[i].name, name))
			return &d->listeners[i];
	return NULL;
}

/* cbId width for a channel id: 0=1 byte, 1=2, 2=4 */
static int cbid_for(uint32_t id)
{
	if (id <= 0xFFu) return 0;
	if (id <= 0xFFFFu) return 1;
	return 2;
}

static void put_cbid(uint8_t* p, size_t* pos, int cbid, uint32_t id)
{
	if (cbid == 0)
		p[(*pos)++] = (uint8_t)id;
	else if (cbid == 1)
	{
		p[(*pos)++] = (uint8_t)id;
		p[(*pos)++] = (uint8_t)(id >> 8);
	}
	else
	{
		p[(*pos)++] = (uint8_t)id;
		p[(*pos)++] = (uint8_t)(id >> 8);
		p[(*pos)++] = (uint8_t)(id >> 16);
		p[(*pos)++] = (uint8_t)(id >> 24);
	}
}

static int get_cbid(const uint8_t* p, size_t len, size_t* pos, int cbid, uint32_t* id)
{
	if (cbid == 0)
	{
		if (*pos + 1 > len) return -1;
		*id = p[(*pos)++];
	}
	else if (cbid == 1)
	{
		if (*pos + 2 > len) return -1;
		*id = (uint32_t)(p[*pos] | (p[*pos + 1] << 8));
		*pos += 2;
	}
	else if (cbid == 2)
	{
		if (*pos + 4 > len) return -1;
		*id = (uint32_t)(p[*pos] | (p[*pos + 1] << 8) | (p[*pos + 2] << 16) | (p[*pos + 3] << 24));
		*pos += 4;
	}
	else
		return -1;
	return 0;
}

static int get_len_field(const uint8_t* p, size_t len, size_t* pos, int lenbits, uint32_t* out)
{
	return get_cbid(p, len, pos, lenbits, out); /* same widths as ChannelId */
}

static int send_raw(struct rdp_dynvc* d, const uint8_t* pdu, size_t len)
{
	if (!d->send)
		return -1;
	return d->send(d->send_user, pdu, len);
}

static int handle_caps(struct rdp_dynvc* d, const uint8_t* p, size_t len)
{
	/* header already consumed: Pad (1) Version (2) [PriorityCharge x4] */
	uint8_t rsp[4];
	uint16_t ver;
	if (len < 3)
		return RDP_E_PROTO;
	ver = (uint16_t)(p[1] | (p[2] << 8));
	if (ver < 1 || ver > 3)
		ver = 1;
	/* we support version 1 (no compressed data, no priority charges needed) */
	if (ver > 1)
		ver = 1;
	d->version = ver;
	rsp[0] = (uint8_t)(0x05 << 4); /* cbId=0 Sp=0 Cmd=CAPS */
	rsp[1] = 0;
	rsp[2] = (uint8_t)ver;
	rsp[3] = (uint8_t)(ver >> 8);
	d->caps_ok = 1;
	return send_raw(d, rsp, 4);
}

/* a slot for a new channel: a closed one again, or the next (review 2 N4) */
static struct rdp_dynvc_chan* free_slot(struct rdp_dynvc* d)
{
	int i;
	for (i = 0; i < d->nchans; i++)
		if (!d->chans[i].open)
			return &d->chans[i];
	if (d->nchans < RDP_DYNVC_MAX_CHANNELS)
		return &d->chans[d->nchans++];
	return NULL;
}

static int handle_create(struct rdp_dynvc* d, int cbid, int pri, const uint8_t* p, size_t len)
{
	uint32_t id = 0;
	size_t pos = 0;
	char name[RDP_DYNVC_NAME_MAX];
	size_t ni = 0;
	struct rdp_dynvc_listener* L;
	struct rdp_dynvc_chan* ch;
	int32_t status = 0;
	uint8_t rsp[16];
	size_t rpos;
	int rc;
	(void)pri;

	d->creates++;
	if (get_cbid(p, len, &pos, cbid, &id) < 0)
		return RDP_E_PROTO;
	while (pos < len && p[pos])
	{
		if (ni + 1 < sizeof(name))
			name[ni] = (char)p[pos];
		ni++;
		pos++;
	}
	if (pos >= len)
		return RDP_E_PROTO; /* missing NUL */
	pos++;
	/* a name of RDP_DYNVC_NAME_MAX bytes or more is no listener's: that
	 * channel is refused, the session goes on (review 2 N6) */
	name[ni < sizeof(name) ? ni : 0] = 0;

	L = ni < sizeof(name) ? find_listener(d, name) : NULL;
	if (!L || !(ch = free_slot(d)))
		status = (int32_t)0x80004005L; /* E_FAIL */
	else if (L->on_create && (rc = L->on_create(d, id, name, L->user)) < 0)
		status = (int32_t)0x80004005L;
	else
	{
		memset(ch, 0, sizeof(*ch));
		ch->id = id;
		ch->open = 1;
		ch->cb_id_bits = cbid;
		strncpy(ch->name, name, RDP_DYNVC_NAME_MAX - 1);
		ch->listener = L;
		status = 0;
	}

	rpos = 0;
	rsp[rpos++] = (uint8_t)((cbid & 3) | (0x01 << 4)); /* Cmd=CREATE */
	put_cbid(rsp, &rpos, cbid, id);
	rsp[rpos++] = (uint8_t)status;
	rsp[rpos++] = (uint8_t)((uint32_t)status >> 8);
	rsp[rpos++] = (uint8_t)((uint32_t)status >> 16);
	rsp[rpos++] = (uint8_t)((uint32_t)status >> 24);
	return send_raw(d, rsp, rpos);
}

static int deliver(struct rdp_dynvc* d, struct rdp_dynvc_chan* ch)
{
	if (!ch->listener || !ch->listener->on_data)
		return 0;
	d->data_msgs++;
	ch->listener->on_data(d, ch->id, ch->asm_buf, ch->asm_have, ch->listener->user);
	free(ch->asm_buf);
	ch->asm_buf = NULL;
	ch->asm_have = ch->asm_total = 0;
	return 0;
}

static int handle_data_first(struct rdp_dynvc* d, int cbid, int lenbits,
                             const uint8_t* p, size_t len)
{
	uint32_t id = 0, total = 0;
	size_t pos = 0;
	struct rdp_dynvc_chan* ch;
	size_t payload;

	if (get_cbid(p, len, &pos, cbid, &id) < 0)
		return RDP_E_PROTO;
	if (get_len_field(p, len, &pos, lenbits, &total) < 0)
		return RDP_E_PROTO;
	ch = rdp_dynvc_find(d, id);
	if (!ch)
		return 0; /* unknown: ignore */
	free(ch->asm_buf);
	ch->asm_buf = NULL;
	ch->asm_have = 0;
	ch->asm_total = total;
	ch->discard = 0;
	if (total > RDP_VC_MAX_MESSAGE)
	{
		/* more than the client will hold (review 2 N5): this message
		 * is read past and dropped; the channel and session go on */
		payload = len - pos;
		if (payload > total)
			return RDP_E_PROTO;
		ch->discard = 1;
		ch->asm_have = payload;
		return 0;
	}
	if (total)
	{
		ch->asm_buf = (uint8_t*)malloc(total);
		if (!ch->asm_buf)
			return -2;
	}
	payload = len - pos;
	if (payload > total)
		return RDP_E_PROTO;
	if (payload && ch->asm_buf)
		memcpy(ch->asm_buf, p + pos, payload);
	ch->asm_have = payload;
	if (ch->asm_have == ch->asm_total)
		return deliver(d, ch);
	return 0;
}

static int handle_data(struct rdp_dynvc* d, int cbid, const uint8_t* p, size_t len)
{
	uint32_t id = 0;
	size_t pos = 0;
	struct rdp_dynvc_chan* ch;
	size_t payload;

	if (get_cbid(p, len, &pos, cbid, &id) < 0)
		return RDP_E_PROTO;
	ch = rdp_dynvc_find(d, id);
	if (!ch)
		return 0;
	payload = len - pos;
	/* unfragmented: DATA without a preceding DATA_FIRST */
	if (!ch->asm_total)
	{
		free(ch->asm_buf);
		ch->asm_buf = payload ? (uint8_t*)malloc(payload) : NULL;
		if (payload && !ch->asm_buf)
			return -2;
		if (payload)
			memcpy(ch->asm_buf, p + pos, payload);
		ch->asm_have = payload;
		ch->asm_total = payload;
		return deliver(d, ch);
	}
	if (ch->asm_have + payload > ch->asm_total)
		return RDP_E_PROTO;
	if (ch->discard)
	{
		ch->asm_have += payload;
		if (ch->asm_have == ch->asm_total)
		{
			ch->discard = 0;
			ch->asm_have = ch->asm_total = 0;
			d->dropped++;
		}
		return 0;
	}
	if (payload && ch->asm_buf)
		memcpy(ch->asm_buf + ch->asm_have, p + pos, payload);
	ch->asm_have += payload;
	if (ch->asm_have == ch->asm_total)
		return deliver(d, ch);
	return 0;
}

static int handle_close(struct rdp_dynvc* d, int cbid, const uint8_t* p, size_t len)
{
	uint32_t id = 0;
	size_t pos = 0;
	struct rdp_dynvc_chan* ch;
	uint8_t rsp[8];
	size_t rpos = 0;

	if (get_cbid(p, len, &pos, cbid, &id) < 0)
		return RDP_E_PROTO;
	ch = rdp_dynvc_find(d, id);
	if (ch)
	{
		if (ch->listener && ch->listener->on_close)
			ch->listener->on_close(d, id, ch->listener->user);
		free(ch->asm_buf);
		ch->asm_buf = NULL;
		ch->open = 0;
	}
	/* echo Close as response (2.2.4: same PDU both ways) */
	rsp[rpos++] = (uint8_t)((cbid & 3) | (0x04 << 4));
	put_cbid(rsp, &rpos, cbid, id);
	return send_raw(d, rsp, rpos);
}

static int handle_softsync(struct rdp_dynvc* d, const uint8_t* p, size_t len)
{
	/* Soft-Sync Response ([MS-RDPEDYC] 2.2.5.2): no multitransport tunnels. */
	uint8_t rsp[6];
	(void)p;
	(void)len;
	rsp[0] = (uint8_t)(0x09 << 4); /* Soft-Sync Response */
	rsp[1] = 0;                    /* Pad */
	rsp[2] = 0; rsp[3] = 0; rsp[4] = 0; rsp[5] = 0; /* NumberOfTunnels = 0 */
	return send_raw(d, rsp, 6);
}

static int handle_pdu(struct rdp_dynvc* d, const uint8_t* p, size_t len)
{
	uint8_t hdr;
	int cbid, sp, cmd;

	if (!p || len < 1)
		return RDP_E_PROTO;
	hdr = p[0];
	cbid = hdr & 3;
	sp = (hdr >> 2) & 3;
	cmd = (hdr >> 4) & 0x0F;
	p++;
	len--;

	switch (cmd)
	{
		case RDP_DYNVC_CMD_CAPS:
			return handle_caps(d, p, len);
		case RDP_DYNVC_CMD_CREATE:
			return handle_create(d, cbid, sp, p, len);
		case RDP_DYNVC_CMD_DATA_FIRST:
			return handle_data_first(d, cbid, sp, p, len);
		case RDP_DYNVC_CMD_DATA:
			return handle_data(d, cbid, p, len);
		case RDP_DYNVC_CMD_CLOSE:
			return handle_close(d, cbid, p, len);
		case RDP_DYNVC_CMD_DATA_FIRST_COMP:
		case RDP_DYNVC_CMD_DATA_COMP:
			/* version 1: we never negotiated compression */
			return RDP_E_PROTO;
		case RDP_DYNVC_CMD_SOFTSYNC_REQ:
			return handle_softsync(d, p, len);
		default:
			return 0; /* ignore unknown */
	}
}

int rdp_dynvc_input(struct rdp_dynvc* d, const uint8_t* data, size_t len)
{
	int rc = rdp_vc_rx_feed(&d->vc, data, len);
	if (rc < 0)
		return rc;
	if (rc == 0)
		return 0;
	rc = handle_pdu(d, d->vc.buf, d->vc.have);
	/* the message is consumed; keep the buffer for the next */
	d->vc.have = 0;
	return rc;
}

int rdp_dynvc_send_data(struct rdp_dynvc* d, uint32_t channel_id,
                        const uint8_t* data, size_t len)
{
	struct rdp_dynvc_chan* ch = rdp_dynvc_find(d, channel_id);
	int cbid;
	uint8_t pdu[1600];
	size_t pos, max_payload, sent = 0;
	int first = 1;

	if (!ch || !d->send)
		return -1;
	cbid = ch->cb_id_bits >= 0 ? ch->cb_id_bits : cbid_for(channel_id);

	while (sent < len || (len == 0 && first))
	{
		size_t hdr = 1 + (cbid == 0 ? 1 : cbid == 1 ? 2 : 4);
		size_t chunk;
		pos = 0;
		/* DATA_FIRST only when the message needs more than one PDU
		 * ([MS-RDPEDYC] 2.2.3.1, 3.1.5.1.4); one that fits goes as DATA */
		if (first && len > 1500)
		{
			int lenbits = len <= 0xFF ? 0 : len <= 0xFFFF ? 1 : 2;
			hdr += (lenbits == 0 ? 1 : lenbits == 1 ? 2 : 4);
			max_payload = (sizeof(pdu) > hdr) ? sizeof(pdu) - hdr : 0;
			if (max_payload > 1500)
				max_payload = 1500;
			chunk = len - sent;
			if (chunk > max_payload)
				chunk = max_payload;
			pdu[pos++] = (uint8_t)((cbid & 3) | ((lenbits & 3) << 2) | (0x02 << 4));
			put_cbid(pdu, &pos, cbid, channel_id);
			put_cbid(pdu, &pos, lenbits, (uint32_t)len);
		}
		else
		{
			max_payload = (sizeof(pdu) > hdr) ? sizeof(pdu) - hdr : 0;
			if (max_payload > 1500)
				max_payload = 1500;
			chunk = len - sent;
			if (chunk > max_payload)
				chunk = max_payload;
			pdu[pos++] = (uint8_t)((cbid & 3) | (0x03 << 4));
			put_cbid(pdu, &pos, cbid, channel_id);
		}
		if (chunk)
			memcpy(pdu + pos, data + sent, chunk);
		pos += chunk;
		if (d->send(d->send_user, pdu, pos) < 0)
			return -1;
		sent += chunk;
		first = 0;
		if (len == 0)
			break;
	}
	return 0;
}
