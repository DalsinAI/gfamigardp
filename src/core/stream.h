/*
 * GFAmigaRDP: bounds-checked byte buffers for building and reading PDUs.
 * RDP's own fields are little-endian; TPKT, X.224 and the BER/PER of MCS
 * are big-endian. A stream that runs past its end sets err and stops
 * moving, so a PDU is built or read in full and checked once at the end.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_STREAM_H
#define RDP_STREAM_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct rdp_stream
{
	uint8_t* p;     /* the buffer */
	size_t size;    /* its size */
	size_t pos;     /* where the next byte goes or comes from */
	int err;        /* 1 once it ran past its end */
};

static inline void rdp_s_init(struct rdp_stream* s, void* buf, size_t size)
{
	s->p = (uint8_t*)buf;
	s->size = size;
	s->pos = 0;
	s->err = 0;
}

static inline int rdp_s_need(struct rdp_stream* s, size_t n)
{
	if (s->err || s->size - s->pos < n)
	{
		s->err = 1;
		return 0;
	}
	return 1;
}

static inline size_t rdp_s_left(const struct rdp_stream* s)
{
	return s->err ? 0 : s->size - s->pos;
}

static inline void rdp_s_u8(struct rdp_stream* s, uint8_t v)
{
	if (rdp_s_need(s, 1))
		s->p[s->pos++] = v;
}

static inline void rdp_s_u16le(struct rdp_stream* s, uint16_t v)
{
	if (rdp_s_need(s, 2))
	{
		s->p[s->pos++] = (uint8_t)v;
		s->p[s->pos++] = (uint8_t)(v >> 8);
	}
}

static inline void rdp_s_u16be(struct rdp_stream* s, uint16_t v)
{
	if (rdp_s_need(s, 2))
	{
		s->p[s->pos++] = (uint8_t)(v >> 8);
		s->p[s->pos++] = (uint8_t)v;
	}
}

static inline void rdp_s_u32le(struct rdp_stream* s, uint32_t v)
{
	rdp_s_u16le(s, (uint16_t)v);
	rdp_s_u16le(s, (uint16_t)(v >> 16));
}

static inline void rdp_s_bytes(struct rdp_stream* s, const void* src, size_t n)
{
	if (n && rdp_s_need(s, n))
	{
		memcpy(s->p + s->pos, src, n);
		s->pos += n;
	}
}

static inline uint8_t rdp_s_get_u8(struct rdp_stream* s)
{
	return rdp_s_need(s, 1) ? s->p[s->pos++] : 0;
}

static inline uint16_t rdp_s_get_u16le(struct rdp_stream* s)
{
	uint16_t v;
	if (!rdp_s_need(s, 2))
		return 0;
	v = (uint16_t)(s->p[s->pos] | (s->p[s->pos + 1] << 8));
	s->pos += 2;
	return v;
}

static inline uint16_t rdp_s_get_u16be(struct rdp_stream* s)
{
	uint16_t v;
	if (!rdp_s_need(s, 2))
		return 0;
	v = (uint16_t)((s->p[s->pos] << 8) | s->p[s->pos + 1]);
	s->pos += 2;
	return v;
}

static inline uint32_t rdp_s_get_u32le(struct rdp_stream* s)
{
	uint32_t lo = rdp_s_get_u16le(s);
	return lo | ((uint32_t)rdp_s_get_u16le(s) << 16);
}

static inline void rdp_s_skip(struct rdp_stream* s, size_t n)
{
	if (rdp_s_need(s, n))
		s->pos += n;
}

#endif
