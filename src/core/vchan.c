/*
 * GFAmigaRDP: static virtual channel framing; see vchan.h.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "vchan.h"
#include "connect.h"

#include <stdlib.h>
#include <string.h>

void rdp_vc_rx_init(struct rdp_vc_rx* r)
{
	memset(r, 0, sizeof(*r));
}

void rdp_vc_rx_free(struct rdp_vc_rx* r)
{
	free(r->buf);
	r->buf = NULL;
	r->size = r->have = r->total = 0;
	r->active = r->skip = 0;
}

static int ensure(struct rdp_vc_rx* r, size_t need)
{
	uint8_t* n;
	size_t cap;
	if (r->size >= need)
		return 0;
	cap = need < 4096 ? 4096 : need;
	n = (uint8_t*)realloc(r->buf, cap);
	if (!n)
		return -2; /* AMIPR_ENOMEM without pulling amipr.h into core */
	r->buf = n;
	r->size = cap;
	return 0;
}

int rdp_vc_rx_feed(struct rdp_vc_rx* r, const uint8_t* data, size_t len)
{
	uint32_t total, flags, chunk;
	int rc;

	if (!data || len < 8)
		return RDP_E_PROTO;
	total = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
	flags = (uint32_t)data[4] | ((uint32_t)data[5] << 8) | ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
	chunk = (uint32_t)(len - 8);

	if (flags & RDP_VC_FLAG_FIRST)
	{
		r->have = 0;
		r->total = total;
		r->active = 1;
		r->skip = total > RDP_VC_MAX_MESSAGE;   /* review 2 N5 */
		if (!r->skip)
		{
			rc = ensure(r, total ? (size_t)total : (size_t)chunk);
			if (rc < 0)
				return rc;
		}
	}
	else if (!r->active)
		return RDP_E_PROTO;

	if (r->total && r->have + chunk > r->total)
		return RDP_E_PROTO;
	if (!r->skip && r->have + chunk > RDP_VC_MAX_MESSAGE)
		r->skip = 1;   /* no length said, and it grew past the limit */
	if (r->skip)
	{
		/* counted, not kept; at LAST the message is dropped */
		r->have += chunk;
		if (flags & RDP_VC_FLAG_LAST)
		{
			r->active = r->skip = 0;
			r->have = 0;
			r->dropped++;
		}
		return 0;
	}
	rc = ensure(r, r->have + chunk);
	if (rc < 0)
		return rc;
	if (chunk)
		memcpy(r->buf + r->have, data + 8, chunk);
	r->have += chunk;

	if (flags & RDP_VC_FLAG_LAST)
	{
		r->active = 0;
		if (r->total && r->have != r->total)
			return RDP_E_PROTO;
		return 1;
	}
	return 0;
}

int rdp_vc_build(uint8_t* out, size_t size, const uint8_t* payload, size_t len, uint32_t flags)
{
	if (!out || size < 8 + len)
		return RDP_E_SPACE;
	out[0] = (uint8_t)len;
	out[1] = (uint8_t)(len >> 8);
	out[2] = (uint8_t)(len >> 16);
	out[3] = (uint8_t)(len >> 24);
	out[4] = (uint8_t)flags;
	out[5] = (uint8_t)(flags >> 8);
	out[6] = (uint8_t)(flags >> 16);
	out[7] = (uint8_t)(flags >> 24);
	if (len && payload)
		memcpy(out + 8, payload, len);
	return (int)(8 + len);
}
