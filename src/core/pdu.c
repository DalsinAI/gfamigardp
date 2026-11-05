/*
 * GFAmigaRDP: Capabilities Exchange and Connection Finalization PDUs; see
 * pdu.h. The capability sets are [MS-RDPBCGR] 2.2.7.1 and 2.2.7.2, with
 * the values the specification's client trace (4.1.13) uses where the
 * choice is free, and none of the drawing orders or caches yet: the
 * server sends bitmaps (or, once the client asks for it, the graphics
 * pipeline over a dynamic channel).
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "pdu.h"

#define CAPSET_GENERAL 1
#define CAPSET_BITMAP 2
#define CAPSET_ORDER 3
#define CAPSET_BITMAPCACHE 4
#define CAPSET_CONTROL 5
#define CAPSET_ACTIVATION 7
#define CAPSET_POINTER 8
#define CAPSET_SHARE 9
#define CAPSET_SOUND 12
#define CAPSET_INPUT 13
#define CAPSET_FONT 14
#define CAPSET_BRUSH 15
#define CAPSET_GLYPHCACHE 16
#define CAPSET_OFFSCREENCACHE 17
#define CAPSET_VIRTUALCHANNEL 20

const uint16_t rdp_client_cap_types[] = {
	CAPSET_GENERAL, CAPSET_BITMAP, CAPSET_ORDER, CAPSET_BITMAPCACHE, CAPSET_POINTER, CAPSET_INPUT,
	CAPSET_BRUSH, CAPSET_GLYPHCACHE, CAPSET_OFFSCREENCACHE, CAPSET_VIRTUALCHANNEL, CAPSET_SOUND,
	CAPSET_SHARE, CAPSET_CONTROL, CAPSET_ACTIVATION, CAPSET_FONT,
};
const int rdp_client_ncaps = (int)(sizeof(rdp_client_cap_types) / sizeof(rdp_client_cap_types[0]));

static void zeros(struct rdp_stream* s, size_t n)
{
	while (n--)
		rdp_s_u8(s, 0);
}

static size_t cap_begin(struct rdp_stream* s, uint16_t type)
{
	size_t at = s->pos;
	rdp_s_u16le(s, type);
	rdp_s_u16le(s, 0);   /* lengthCapability, patched by cap_end */
	return at;
}

static void cap_end(struct rdp_stream* s, size_t at)
{
	size_t n = s->pos - at;
	if (!s->err)
	{
		s->p[at + 2] = (uint8_t)n;
		s->p[at + 3] = (uint8_t)(n >> 8);
	}
}

static void caps(struct rdp_stream* s, uint16_t w, uint16_t h, uint16_t bpp)
{
	size_t at;

	/* TS_GENERAL_CAPABILITYSET (2.2.7.1.1): 24 bytes */
	at = cap_begin(s, CAPSET_GENERAL);
	rdp_s_u16le(s, 0);              /* osMajorType: unspecified */
	rdp_s_u16le(s, 0);              /* osMinorType */
	rdp_s_u16le(s, 0x0200);         /* protocolVersion TS_CAPS_PROTOCOLVERSION */
	rdp_s_u16le(s, 0);              /* pad2octetsA */
	rdp_s_u16le(s, 0);              /* generalCompressionTypes */
	rdp_s_u16le(s, 0x0001 | 0x0004 | 0x0400);  /* FASTPATH_OUTPUT_SUPPORTED, LONG_CREDENTIALS_SUPPORTED, NO_BITMAP_COMPRESSION_HDR */
	rdp_s_u16le(s, 0);              /* updateCapabilityFlag */
	rdp_s_u16le(s, 0);              /* remoteUnshareFlag */
	rdp_s_u16le(s, 0);              /* generalCompressionLevel */
	rdp_s_u8(s, 1);                 /* refreshRectSupport */
	rdp_s_u8(s, 1);                 /* suppressOutputSupport */
	cap_end(s, at);

	/* TS_BITMAP_CAPABILITYSET (2.2.7.1.2): 28 bytes */
	at = cap_begin(s, CAPSET_BITMAP);
	rdp_s_u16le(s, bpp);            /* preferredBitsPerPixel */
	rdp_s_u16le(s, 1);              /* receive1BitPerPixel */
	rdp_s_u16le(s, 1);              /* receive4BitsPerPixel */
	rdp_s_u16le(s, 1);              /* receive8BitsPerPixel */
	rdp_s_u16le(s, w);
	rdp_s_u16le(s, h);
	rdp_s_u16le(s, 0);              /* pad2octets */
	rdp_s_u16le(s, 1);              /* desktopResizeFlag */
	rdp_s_u16le(s, 1);              /* bitmapCompressionFlag: MUST be TRUE */
	rdp_s_u8(s, 0);                 /* highColorFlags */
	rdp_s_u8(s, 0);                 /* drawingFlags */
	rdp_s_u16le(s, 1);              /* multipleRectangleSupport */
	rdp_s_u16le(s, 0);              /* pad2octetsB */
	cap_end(s, at);

	/* TS_ORDER_CAPABILITYSET (2.2.7.1.3): 88 bytes, no orders */
	at = cap_begin(s, CAPSET_ORDER);
	zeros(s, 16);                   /* terminalDescriptor */
	rdp_s_u32le(s, 0);              /* pad4octetsA */
	rdp_s_u16le(s, 1);              /* desktopSaveXGranularity */
	rdp_s_u16le(s, 20);             /* desktopSaveYGranularity */
	rdp_s_u16le(s, 0);              /* pad2octetsA */
	rdp_s_u16le(s, 1);              /* maximumOrderLevel ORD_LEVEL_1_ORDERS */
	rdp_s_u16le(s, 0);              /* numberFonts */
	rdp_s_u16le(s, 0x0002 | 0x0008 | 0x0020);  /* NEGOTIATEORDERSUPPORT, ZEROBOUNDSDELTASSUPPORT, COLORINDEXSUPPORT */
	zeros(s, 32);                   /* orderSupport: none */
	rdp_s_u16le(s, 0);              /* textFlags */
	rdp_s_u16le(s, 0);              /* orderSupportExFlags */
	rdp_s_u32le(s, 0);              /* pad4octetsB */
	rdp_s_u32le(s, 0);              /* desktopSaveSize */
	rdp_s_u16le(s, 0);              /* pad2octetsC */
	rdp_s_u16le(s, 0);              /* pad2octetsD */
	rdp_s_u16le(s, 0);              /* textANSICodePage */
	rdp_s_u16le(s, 0);              /* pad2octetsE */
	cap_end(s, at);

	/* TS_BITMAPCACHE_CAPABILITYSET (2.2.7.1.4.1): 40 bytes, no cache */
	at = cap_begin(s, CAPSET_BITMAPCACHE);
	zeros(s, 24);                   /* pad1 to pad6 */
	zeros(s, 12);                   /* Cache0..2 entries and cell sizes */
	cap_end(s, at);

	/* TS_POINTER_CAPABILITYSET (2.2.7.1.5): 10 bytes */
	at = cap_begin(s, CAPSET_POINTER);
	rdp_s_u16le(s, 1);              /* colorPointerFlag */
	rdp_s_u16le(s, 20);             /* colorPointerCacheSize */
	rdp_s_u16le(s, 21);             /* pointerCacheSize */
	cap_end(s, at);

	/* TS_INPUT_CAPABILITYSET (2.2.7.1.6): 88 bytes */
	at = cap_begin(s, CAPSET_INPUT);
	rdp_s_u16le(s, 0x0001 | 0x0004 | 0x0010 | 0x0020);  /* SCANCODES, MOUSEX, UNICODE, FASTPATH_INPUT2 */
	rdp_s_u16le(s, 0);              /* pad2octetsA */
	rdp_s_u32le(s, 0x00000409);     /* keyboardLayout: US */
	rdp_s_u32le(s, 4);              /* keyboardType: IBM enhanced */
	rdp_s_u32le(s, 0);              /* keyboardSubType */
	rdp_s_u32le(s, 12);             /* keyboardFunctionKey */
	zeros(s, 64);                   /* imeFileName */
	cap_end(s, at);

	/* TS_BRUSH_CAPABILITYSET (2.2.7.1.7): 8 bytes */
	at = cap_begin(s, CAPSET_BRUSH);
	rdp_s_u32le(s, 0);              /* BRUSH_DEFAULT */
	cap_end(s, at);

	/* TS_GLYPHCACHE_CAPABILITYSET (2.2.7.1.8): 52 bytes, no glyph cache */
	at = cap_begin(s, CAPSET_GLYPHCACHE);
	zeros(s, 40);                   /* GlyphCache[10] */
	rdp_s_u32le(s, 0);              /* FragCache */
	rdp_s_u16le(s, 0);              /* GlyphSupportLevel GLYPH_SUPPORT_NONE */
	rdp_s_u16le(s, 0);              /* pad2octets */
	cap_end(s, at);

	/* TS_OFFSCREEN_CAPABILITYSET (2.2.7.1.9): 12 bytes, none */
	at = cap_begin(s, CAPSET_OFFSCREENCACHE);
	rdp_s_u32le(s, 0);
	rdp_s_u16le(s, 0);
	rdp_s_u16le(s, 0);
	cap_end(s, at);

	/* TS_VIRTUALCHANNEL_CAPABILITYSET (2.2.7.1.10): 12 bytes */
	at = cap_begin(s, CAPSET_VIRTUALCHANNEL);
	rdp_s_u32le(s, 0);              /* VCCAPS_NO_COMPR */
	rdp_s_u32le(s, 1600);           /* VCChunkSize */
	cap_end(s, at);

	/* TS_SOUND_CAPABILITYSET (2.2.7.1.11): 8 bytes */
	at = cap_begin(s, CAPSET_SOUND);
	rdp_s_u16le(s, 0);
	rdp_s_u16le(s, 0);
	cap_end(s, at);

	/* TS_SHARE_CAPABILITYSET (2.2.7.2.3): 8 bytes */
	at = cap_begin(s, CAPSET_SHARE);
	rdp_s_u16le(s, 0);              /* nodeId: the client sends 0 */
	rdp_s_u16le(s, 0);
	cap_end(s, at);

	/* TS_CONTROL_CAPABILITYSET (2.2.7.2.2): 12 bytes */
	at = cap_begin(s, CAPSET_CONTROL);
	rdp_s_u16le(s, 0);              /* controlFlags */
	rdp_s_u16le(s, 0);              /* remoteDetachFlag */
	rdp_s_u16le(s, 2);              /* controlInterest CONTROLPRIORITY_NEVER */
	rdp_s_u16le(s, 2);              /* detachInterest CONTROLPRIORITY_NEVER */
	cap_end(s, at);

	/* TS_WINDOWACTIVATION_CAPABILITYSET (2.2.7.2.4): 12 bytes */
	at = cap_begin(s, CAPSET_ACTIVATION);
	zeros(s, 8);
	cap_end(s, at);

	/* TS_FONT_CAPABILITYSET (2.2.7.2.5): 8 bytes */
	at = cap_begin(s, CAPSET_FONT);
	rdp_s_u16le(s, 0x0001);         /* FONTSUPPORT_FONTLIST */
	rdp_s_u16le(s, 0);
	cap_end(s, at);
}

int rdp_build_confirm_active(struct rdp_stream* s, const struct rdp_demand* d, uint16_t user,
                             uint16_t w, uint16_t h, uint16_t bpp)
{
	static const char src[] = "MSTSC";   /* sourceDescriptor, as 4.1.13 has it */
	size_t start = s->pos, lencombined_at, caps_at;
	rdp_s_u16le(s, 0);                   /* totalLength, patched */
	rdp_s_u16le(s, RDP_PDUTYPE_CONFIRMACTIVE | RDP_PDUTYPE_VERSION);
	rdp_s_u16le(s, user);                /* pduSource */
	rdp_s_u32le(s, d->share_id);
	rdp_s_u16le(s, RDP_SERVER_CHANNEL_ID);   /* originatorId */
	rdp_s_u16le(s, sizeof(src));         /* lengthSourceDescriptor, its NUL too */
	lencombined_at = s->pos;
	rdp_s_u16le(s, 0);                   /* lengthCombinedCapabilities, patched */
	rdp_s_bytes(s, src, sizeof(src));
	caps_at = s->pos;
	rdp_s_u16le(s, (uint16_t)rdp_client_ncaps);
	rdp_s_u16le(s, 0);                   /* pad2Octets */
	caps(s, w, h, bpp);
	if (s->err)
		return -1;
	{
		size_t total = s->pos - start, combined = s->pos - caps_at;
		s->p[start] = (uint8_t)total;
		s->p[start + 1] = (uint8_t)(total >> 8);
		s->p[lencombined_at] = (uint8_t)combined;
		s->p[lencombined_at + 1] = (uint8_t)(combined >> 8);
		return (int)total;
	}
}

int rdp_build_data(struct rdp_stream* s, uint32_t share_id, uint16_t user, uint8_t type2,
                   const uint8_t* payload, size_t len)
{
	size_t total = 18 + len;
	rdp_s_u16le(s, (uint16_t)total);
	rdp_s_u16le(s, RDP_PDUTYPE_DATA | RDP_PDUTYPE_VERSION);
	rdp_s_u16le(s, user);
	rdp_s_u32le(s, share_id);
	rdp_s_u8(s, 0);                      /* pad1 */
	rdp_s_u8(s, 1);                      /* streamId STREAM_LOW */
	rdp_s_u16le(s, (uint16_t)(total - 14));  /* uncompressedLength, as 4.1.14 has it */
	rdp_s_u8(s, type2);
	rdp_s_u8(s, 0);                      /* compressedType */
	rdp_s_u16le(s, 0);                   /* compressedLength */
	rdp_s_bytes(s, payload, len);
	return s->err ? -1 : (int)total;
}

void rdp_payload_sync(uint8_t out[4])
{
	out[0] = 1; out[1] = 0;              /* messageType SYNCMSGTYPE_SYNC */
	out[2] = (uint8_t)RDP_SERVER_CHANNEL_ID;
	out[3] = (uint8_t)(RDP_SERVER_CHANNEL_ID >> 8);
}

void rdp_payload_control(uint8_t out[8], uint16_t action)
{
	memset(out, 0, 8);
	out[0] = (uint8_t)action;
	out[1] = (uint8_t)(action >> 8);
}

void rdp_payload_fontlist(uint8_t out[8])
{
	memset(out, 0, 8);                   /* numberFonts 0, totalNumFonts 0 */
	out[4] = 0x03;                       /* listFlags FONTLIST_FIRST | FONTLIST_LAST */
	out[6] = 0x32;                       /* entrySize 50 */
}

int rdp_share_parse(const uint8_t* p, size_t len, struct rdp_share* o)
{
	struct rdp_stream s;
	uint16_t total, type;
	memset(o, 0, sizeof(*o));
	rdp_s_init(&s, (void*)p, len);
	total = rdp_s_get_u16le(&s);
	if (total == 0x8000)                 /* a flow PDU (2.2.8.1.1.1.1): nothing to do */
		return s.err ? RDP_E_PROTO_PDU : 0;
	type = rdp_s_get_u16le(&s);
	o->type = type & 0x0F;
	o->source = rdp_s_get_u16le(&s);
	if (s.err || total < 6 || total > len)
		return RDP_E_PROTO_PDU;
	if (o->type != RDP_PDUTYPE_DATA)
	{
		o->body = p + 6;
		o->body_len = total - 6u;
		return 0;
	}
	o->share_id = rdp_s_get_u32le(&s);
	rdp_s_skip(&s, 1 + 1 + 2);           /* pad1, streamId, uncompressedLength */
	o->type2 = rdp_s_get_u8(&s);
	if (rdp_s_get_u8(&s) & 0x20)         /* compressedType PACKET_COMPRESSED: not asked for */
		return RDP_E_PROTO_PDU;
	rdp_s_skip(&s, 2);
	/* a data PDU shorter than its own 18-byte header: total - s.pos would
	 * wrap round to a huge body_len (review M6) */
	if (s.err || total < s.pos)
		return RDP_E_PROTO_PDU;
	o->body = p + s.pos;
	o->body_len = total - s.pos;
	if (o->type2 == RDP_PDUTYPE2_CONTROL && o->body_len >= 2)
		o->action = (uint16_t)(o->body[0] | (o->body[1] << 8));
	if (o->type2 == RDP_PDUTYPE2_SET_ERROR_INFO && o->body_len >= 4)
		o->error_info = (uint32_t)o->body[0] | ((uint32_t)o->body[1] << 8) | ((uint32_t)o->body[2] << 16) |
		                ((uint32_t)o->body[3] << 24);
	return 0;
}

int rdp_demand_parse(const uint8_t* p, size_t len, struct rdp_demand* d)
{
	struct rdp_stream s;
	uint16_t lsrc, lcaps, i;
	size_t caps_end;
	memset(d, 0, sizeof(*d));
	rdp_s_init(&s, (void*)p, len);
	d->share_id = rdp_s_get_u32le(&s);
	lsrc = rdp_s_get_u16le(&s);
	lcaps = rdp_s_get_u16le(&s);
	rdp_s_skip(&s, lsrc);
	if (s.err || lcaps < 4 || lcaps > rdp_s_left(&s))
		return RDP_E_PROTO_PDU;
	caps_end = s.pos + lcaps;
	d->ncaps = rdp_s_get_u16le(&s);
	rdp_s_skip(&s, 2);
	for (i = 0; i < d->ncaps && !s.err && s.pos + 4 <= caps_end; i++)
	{
		size_t at = s.pos;
		uint16_t type = rdp_s_get_u16le(&s), l = rdp_s_get_u16le(&s);
		if (l < 4 || at + l > caps_end)
			return RDP_E_PROTO_PDU;
		if (type < 32)
			d->cap_mask |= 1UL << type;
		if (type == CAPSET_BITMAP && l >= 16)
		{
			d->bpp = rdp_s_get_u16le(&s);
			rdp_s_skip(&s, 6);
			d->width = rdp_s_get_u16le(&s);
			d->height = rdp_s_get_u16le(&s);
		}
		s.pos = at + l;
	}
	s.pos = caps_end;
	if (rdp_s_left(&s) >= 4)
		d->session_id = rdp_s_get_u32le(&s);
	return s.err ? RDP_E_PROTO_PDU : 0;
}

int rdp_fastpath_count(const uint8_t* p, size_t len, struct rdp_fp_counts* c)
{
	size_t at = 0;
	c->pdus++;
	c->bytes += (uint32_t)len;
	while (at + 3 <= len)
	{
		uint8_t hdr = p[at];
		size_t size = (size_t)p[at + 1 + ((hdr >> 6) & 2 ? 1 : 0)] |
		              ((size_t)p[at + 2 + ((hdr >> 6) & 2 ? 1 : 0)] << 8);
		size_t head = 3 + ((hdr >> 6) & 2 ? 1 : 0);   /* fragmentation and compression: 2 bits each */
		/* updateHeader: updateCode 4 bits, fragmentation 2, compression 2;
		 * compressionFlags (1 byte) only when compression is FASTPATH_OUTPUT_COMPRESSION_USED */
		c->updates++;
		c->by_code[hdr & 15]++;
		at += head + size;
	}
	return at == len ? 0 : RDP_E_PROTO_PDU;
}
