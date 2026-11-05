/*
 * GFAmigaRDP: MCS, GCC and the Client Info PDU up to licensing; see mcs.h.
 * The byte layouts follow [MS-RDPBCGR] 2.2.1.3 to 2.2.1.12 and the
 * annotated traces in 4.1.3 to 4.1.11; MCS is T.125 (BER for Connect
 * Initial/Response, ALIGNED PER for the domain PDUs), GCC is T.124 PER.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "mcs.h"
#include "stream.h"
#include "utf16.h"
#include "amipr.h"
#include <exec/memory.h>
#include <proto/exec.h>
#include <string.h>

#define BUF 16384
#define MCS_BASE_CHANNEL 1001   /* T.125 initiators are sent less 1001 */

/* DomainMCSPDU choices, shifted into the first byte (T.125 ALIGNED PER) */
#define MCS_EDRQ 1
#define MCS_DPUM 8
#define MCS_AURQ 10
#define MCS_AUCF 11
#define MCS_CJRQ 14
#define MCS_CJCF 15
#define MCS_SDRQ 25
#define MCS_SDIN 26

#define SEC_ENCRYPT 0x0008
#define SEC_INFO_PKT 0x0040
#define SEC_LICENSE_PKT 0x0080

/* ---- TPKT + X.224 Data ---- */

static void tpkt_begin(struct rdp_stream* s, void* buf, size_t size)
{
	rdp_s_init(s, buf, size);
	rdp_s_u8(s, RDP_TPKT_VERSION);
	rdp_s_u8(s, 0);
	rdp_s_u16be(s, 0);            /* patched in tpkt_send */
	rdp_s_u8(s, 2);               /* X.224 Data TPDU: LI 2, DT, EOT */
	rdp_s_u8(s, RDP_X224_DT);
	rdp_s_u8(s, 0x80);
}

static int tpkt_send(struct rdp_transport* t, struct rdp_stream* s)
{
	if (s->err || s->pos > RDP_TPKT_MAX)
		return RDP_E_SPACE;
	s->p[2] = (uint8_t)(s->pos >> 8);
	s->p[3] = (uint8_t)s->pos;
	return rdp_transport_write(t, s->p, s->pos);
}

static int tpkt_data(uint8_t* buf, size_t n, struct rdp_stream* s);

/* one TPKT holding an X.224 Data TPDU: s is set on what follows it */
static int tpkt_read(struct rdp_transport* t, uint8_t* buf, struct rdp_stream* s)
{
	int n = rdp_transport_read_tpkt(t, buf, BUF);
	if (n < 0)
		return n;
	return tpkt_data(buf, (size_t)n, s);
}

/* the X.224 Data TPDU in a TPKT of n bytes: s is set on what follows it */
static int tpkt_data(uint8_t* buf, size_t n, struct rdp_stream* s)
{
	rdp_s_init(s, buf, n);
	rdp_s_skip(s, RDP_TPKT_HEADER);
	if (rdp_s_get_u8(s) != 2 || rdp_s_get_u8(s) != RDP_X224_DT)
		return RDP_E_PROTO;
	rdp_s_skip(s, 1);
	return s->err ? RDP_E_PROTO : 0;
}

/* ---- BER (T.125 Connect PDUs) and PER lengths ---- */

static size_t ber_len_size(size_t n) { return n < 0x80 ? 1 : n < 0x100 ? 2 : 3; }

static void ber_len(struct rdp_stream* s, size_t n)
{
	if (n < 0x80)
		rdp_s_u8(s, (uint8_t)n);
	else if (n < 0x100)
	{
		rdp_s_u8(s, 0x81);
		rdp_s_u8(s, (uint8_t)n);
	}
	else
	{
		rdp_s_u8(s, 0x82);
		rdp_s_u16be(s, (uint16_t)n);
	}
}

static size_t ber_get_len(struct rdp_stream* s)
{
	uint8_t b = rdp_s_get_u8(s);
	size_t n = 0;
	int i;
	if (b < 0x80)
		return b;
	b &= 0x7f;
	if (b > 3)
	{
		s->err = 1;
		return 0;
	}
	for (i = 0; i < b; i++)
		n = (n << 8) | rdp_s_get_u8(s);
	return n;
}

/* a tag and its length; 0 when the tag is another */
static size_t ber_get_tag(struct rdp_stream* s, unsigned tag)
{
	if (tag > 0xff)
	{
		if (rdp_s_get_u8(s) != (tag >> 8))
			s->err = 1;
	}
	if (rdp_s_get_u8(s) != (tag & 0xff))
		s->err = 1;
	return s->err ? 0 : ber_get_len(s);
}

/* INTEGER in its fewest bytes, as the traces in 4.1.3 have it */
static size_t ber_int_size(uint32_t v) { return 2 + (v > 0xffffff ? 4 : v > 0xffff ? 3 : v > 0xff ? 2 : 1); }

static void ber_int(struct rdp_stream* s, uint32_t v)
{
	size_t n = ber_int_size(v) - 2;
	rdp_s_u8(s, 0x02);
	rdp_s_u8(s, (uint8_t)n);
	while (n--)
		rdp_s_u8(s, (uint8_t)(v >> (8 * n)));
}

static void per_len(struct rdp_stream* s, size_t n)
{
	if (n < 0x80)
		rdp_s_u8(s, (uint8_t)n);
	else
		rdp_s_u16be(s, (uint16_t)(0x8000 | n));
}

static size_t per_len_size(size_t n) { return n < 0x80 ? 1 : 2; }

static size_t per_get_len(struct rdp_stream* s)
{
	uint8_t b = rdp_s_get_u8(s);
	if (b & 0x80)
		return ((size_t)(b & 0x7f) << 8) | rdp_s_get_u8(s);
	return b;
}

/* ---- client data blocks (2.2.1.3.2 to 2.2.1.3.5) ---- */

#define CS_CORE_LEN 216     /* through serverSelectedProtocol */
#define CS_SECURITY_LEN 12
#define CS_CLUSTER_LEN 12

static void utf16_fixed(struct rdp_stream* s, const char* str, size_t bytes)
{
	size_t i = 0;
	const uint8_t* p = (const uint8_t*)(str ? str : "");
	while (*p && i + 2 < bytes)   /* leaves room for the terminating NUL */
	{
		rdp_s_u16le(s, *p++);     /* Latin-1 is the first 256 code points */
		i += 2;
	}
	for (; i < bytes; i += 2)
		rdp_s_u16le(s, 0);
}

static void client_data(struct rdp_stream* s, const struct rdp_settings* set, uint32_t selected)
{
	uint16_t high = set->depth == 8 ? 8 : set->depth == 15 ? 15 : set->depth == 24 ? 24 : 16;
	/* TS_UD_CS_CORE */
	rdp_s_u16le(s, 0xC001);
	rdp_s_u16le(s, CS_CORE_LEN);
	rdp_s_u32le(s, 0x00080004);          /* RDP 5.0 and later */
	rdp_s_u16le(s, set->width);
	rdp_s_u16le(s, set->height);
	rdp_s_u16le(s, 0xCA01);              /* colorDepth RNS_UD_COLOR_8BPP; see highColorDepth */
	rdp_s_u16le(s, 0xAA03);              /* SASSequence RNS_UD_SAS_DEL */
	rdp_s_u32le(s, 0x00000409);          /* keyboardLayout: US */
	rdp_s_u32le(s, 2600);                /* clientBuild */
	utf16_fixed(s, "AMIGA", 32);         /* clientName */
	rdp_s_u32le(s, 4);                   /* keyboardType: IBM enhanced (101/102) */
	rdp_s_u32le(s, 0);                   /* keyboardSubType */
	rdp_s_u32le(s, 12);                  /* keyboardFunctionKey */
	utf16_fixed(s, "", 64);              /* imeFileName */
	rdp_s_u16le(s, 0xCA01);              /* postBeta2ColorDepth */
	rdp_s_u16le(s, 1);                   /* clientProductId */
	rdp_s_u32le(s, 0);                   /* serialNumber */
	rdp_s_u16le(s, high);                /* highColorDepth */
	rdp_s_u16le(s, 0x0007);              /* supportedColorDepths: 24, 16, 15 bpp */
	rdp_s_u16le(s, 0x0001);              /* earlyCapabilityFlags: RNS_UD_CS_SUPPORT_ERRINFO_PDU */
	utf16_fixed(s, "", 64);              /* clientDigProductId */
	rdp_s_u8(s, 0);                      /* connectionType (not valid: no flag) */
	rdp_s_u8(s, 0);                      /* pad1octet */
	rdp_s_u32le(s, selected);            /* serverSelectedProtocol: MUST be what the server chose */
	/* TS_UD_CS_CLUSTER */
	rdp_s_u16le(s, 0xC004);
	rdp_s_u16le(s, CS_CLUSTER_LEN);
	rdp_s_u32le(s, 0x0000000D);          /* REDIRECTION_SUPPORTED, REDIRECTION_VERSION4 */
	rdp_s_u32le(s, 0);                   /* RedirectedSessionID */
	/* TS_UD_CS_SEC: the methods of standard RDP security; with TLS/NLA the
	 * server answers ENCRYPTION_METHOD_NONE (5.4.1) */
	rdp_s_u16le(s, 0xC002);
	rdp_s_u16le(s, CS_SECURITY_LEN);
	rdp_s_u32le(s, 0x0000001B);          /* 40BIT, 128BIT, 56BIT, FIPS */
	rdp_s_u32le(s, 0);                   /* extEncryptionMethods */
	/* no TS_UD_CS_NET: no static virtual channels yet */
}

#define CLIENT_DATA_LEN (CS_CORE_LEN + CS_CLUSTER_LEN + CS_SECURITY_LEN)

static void domain_params(struct rdp_stream* s, const uint32_t v[8])
{
	size_t n = 0;
	int i;
	for (i = 0; i < 8; i++)
		n += ber_int_size(v[i]);
	rdp_s_u8(s, 0x30);
	ber_len(s, n);
	for (i = 0; i < 8; i++)
		ber_int(s, v[i]);
}

static size_t domain_params_size(const uint32_t v[8])
{
	size_t n = 0;
	int i;
	for (i = 0; i < 8; i++)
		n += ber_int_size(v[i]);
	return 1 + ber_len_size(n) + n;
}

/* maxChannelIds, maxUserIds, maxTokenIds, numPriorities, minThroughput,
 * maxHeight, maxMCSPDUsize, protocolVersion (4.1.3) */
static const uint32_t dp_target[8] = { 34, 2, 0, 1, 0, 1, 65535, 2 };
static const uint32_t dp_min[8] = { 1, 1, 1, 1, 0, 1, 1056, 2 };
static const uint32_t dp_max[8] = { 65535, 64535, 65535, 1, 0, 1, 65535, 2 };

static int send_connect_initial(struct rdp_transport* t, uint8_t* buf, const struct rdp_settings* set, uint32_t selected)
{
	struct rdp_stream s;
	/* GCC ConnectData + ConferenceCreateRequest (T.124, 4.1.3) */
	size_t ccrq = 12 + per_len_size(CLIENT_DATA_LEN) + CLIENT_DATA_LEN;
	size_t gcc = 7 + per_len_size(ccrq) + ccrq;
	size_t body = 3 + 3 + 3 + domain_params_size(dp_target) + domain_params_size(dp_min) +
	              domain_params_size(dp_max) + 1 + ber_len_size(gcc) + gcc;
	static const uint8_t gcc_head[7] = { 0x00, 0x05, 0x00, 0x14, 0x7c, 0x00, 0x01 };
	static const uint8_t ccrq_head[12] = { 0x00, 0x08, 0x00, 0x10, 0x00, 0x01, 0xc0, 0x00, 'D', 'u', 'c', 'a' };

	tpkt_begin(&s, buf, BUF);
	rdp_s_u8(&s, 0x7f);                  /* [APPLICATION 101] Connect-Initial */
	rdp_s_u8(&s, 0x65);
	ber_len(&s, body);
	rdp_s_u8(&s, 0x04); rdp_s_u8(&s, 1); rdp_s_u8(&s, 1);       /* callingDomainSelector */
	rdp_s_u8(&s, 0x04); rdp_s_u8(&s, 1); rdp_s_u8(&s, 1);       /* calledDomainSelector */
	rdp_s_u8(&s, 0x01); rdp_s_u8(&s, 1); rdp_s_u8(&s, 0xff);    /* upwardFlag TRUE */
	domain_params(&s, dp_target);
	domain_params(&s, dp_min);
	domain_params(&s, dp_max);
	rdp_s_u8(&s, 0x04);                  /* userData */
	ber_len(&s, gcc);
	rdp_s_bytes(&s, gcc_head, sizeof(gcc_head));
	per_len(&s, ccrq);
	rdp_s_bytes(&s, ccrq_head, sizeof(ccrq_head));
	per_len(&s, CLIENT_DATA_LEN);
	client_data(&s, set, selected);
	return tpkt_send(t, &s);
}

static int read_connect_response(struct rdp_transport* t, uint8_t* buf, struct rdp_mcs_result* r)
{
	struct rdp_stream s;
	size_t n, end;
	int rc = tpkt_read(t, buf, &s);
	if (rc < 0)
		return rc;
	ber_get_tag(&s, 0x7f66);             /* [APPLICATION 102] Connect-Response */
	if (ber_get_tag(&s, 0x0a) != 1)      /* result ENUMERATED */
		return RDP_E_PROTO;
	r->connect_result = rdp_s_get_u8(&s);
	if (r->connect_result != 0)
		return s.err ? RDP_E_PROTO : RDP_E_MCS;
	n = ber_get_tag(&s, 0x02);           /* calledConnectId */
	rdp_s_skip(&s, n);
	n = ber_get_tag(&s, 0x30);           /* domainParameters */
	rdp_s_skip(&s, n);
	n = ber_get_tag(&s, 0x04);           /* userData: GCC ConnectData */
	if (s.err || n > rdp_s_left(&s))
		return RDP_E_PROTO;
	end = s.pos + n;
	/* T.124 ConnectData, ConferenceCreateResponse, then the H.221 key "McDn" */
	rdp_s_skip(&s, 7);
	per_get_len(&s);
	while (!s.err && s.pos + 4 <= end && memcmp(s.p + s.pos, "McDn", 4) != 0)
		s.pos++;
	rdp_s_skip(&s, 4);
	n = per_get_len(&s);
	if (s.err || s.pos + n > end)
		return RDP_E_PROTO;
	end = s.pos + n;
	while (!s.err && s.pos + 4 <= end)
	{
		size_t at = s.pos;
		uint16_t type = rdp_s_get_u16le(&s);
		uint16_t len = rdp_s_get_u16le(&s);
		if (len < 4 || at + len > end)
			return RDP_E_PROTO;
		switch (type)
		{
			case 0x0C01:   /* SC_CORE */
				r->server_version = rdp_s_get_u32le(&s);
				if (len >= 12) r->client_requested = rdp_s_get_u32le(&s);
				if (len >= 16) r->early_caps = rdp_s_get_u32le(&s);
				break;
			case 0x0C02:   /* SC_SECURITY */
				r->enc_method = rdp_s_get_u32le(&s);
				r->enc_level = rdp_s_get_u32le(&s);
				break;
			case 0x0C03:   /* SC_NET */
			{
				uint16_t i, count;
				r->io_channel = rdp_s_get_u16le(&s);
				count = rdp_s_get_u16le(&s);
				for (i = 0; i < count; i++)
				{
					uint16_t id = rdp_s_get_u16le(&s);
					if (r->channel_count < RDP_MCS_MAX_CHANNELS)
						r->channels[r->channel_count++] = id;
				}
				break;
			}
			case 0x0C04:   /* SC_MCS_MSGCHANNEL */
				r->msg_channel = rdp_s_get_u16le(&s);
				break;
			default:
				break;
		}
		s.pos = at + len;
	}
	return s.err || !r->io_channel ? RDP_E_PROTO : 0;
}

/* a short domain PDU */
static int send_domain(struct rdp_transport* t, uint8_t* buf, const uint8_t* pdu, size_t len)
{
	struct rdp_stream s;
	tpkt_begin(&s, buf, BUF);
	rdp_s_bytes(&s, pdu, len);
	return tpkt_send(t, &s);
}

/* the next domain PDU; a Disconnect Provider Ultimatum ends the connection */
/* The first byte holds the 6-bit DomainMCSPDU choice, then (ALIGNED PER)
 * the bits of the fields before the first octet-aligned one: for AUcf and
 * CJcf the OPTIONAL-present bit and the first bit of the 4-bit Result, whose
 * other 3 bits top the second byte; for DPUm the 3-bit Reason. */
static int read_domain(struct rdp_transport* t, uint8_t* buf, struct rdp_stream* s, struct rdp_mcs_result* r,
                       unsigned* choice, unsigned* result)
{
	uint8_t b, c;
	int rc = tpkt_read(t, buf, s);
	if (rc < 0)
		return rc;
	b = rdp_s_get_u8(s);
	*choice = b >> 2;
	if (*choice == MCS_DPUM)
	{
		c = rdp_s_get_u8(s);
		r->disconnect_reason = 1 + (((b & 3u) << 1) | (c >> 7));
		return AMIPR_ECLOSED;
	}
	if (*choice == MCS_AUCF || *choice == MCS_CJCF)
	{
		c = rdp_s_get_u8(s);
		*result = ((b & 1u) << 3) | (c >> 5);
	}
	return s->err ? RDP_E_PROTO : 0;
}

static int join(struct rdp_transport* t, uint8_t* buf, struct rdp_mcs_result* r, uint16_t channel)
{
	struct rdp_stream s;
	uint8_t pdu[5];
	unsigned choice = 0;
	int rc;
	pdu[0] = MCS_CJRQ << 2;
	pdu[1] = (uint8_t)((r->user_channel - MCS_BASE_CHANNEL) >> 8);
	pdu[2] = (uint8_t)(r->user_channel - MCS_BASE_CHANNEL);
	pdu[3] = (uint8_t)(channel >> 8);
	pdu[4] = (uint8_t)channel;
	rc = send_domain(t, buf, pdu, 5);
	if (rc < 0)
		return rc;
	rc = read_domain(t, buf, &s, r, &choice, &r->join_result);
	if (rc < 0)
		return rc;
	if (choice != MCS_CJCF)
		return RDP_E_PROTO;
	if (r->join_result != 0)
	{
		r->join_failed = channel;
		return RDP_E_MCS;
	}
	return 0;
}

/* ---- Client Info PDU (2.2.1.11) ---- */

/* the strings as UTF-8 to UTF-16LE, decoded as gfopenntlm decodes them
 * for NLA (utf16.h), so a user name or password with letters beyond
 * ASCII is the same text in the NTLM AUTHENTICATE, the TSCredentials and
 * here; ending in a NUL code unit */
static void utf16z(struct rdp_stream* s, const char* p)
{
	size_t n = rdp_utf16_len(p);
	if (rdp_s_need(s, n + 2))
	{
		s->pos += rdp_utf16_put(p, s->p + s->pos, n);
		s->p[s->pos++] = 0;
		s->p[s->pos++] = 0;
	}
}

static int send_client_info(struct rdp_transport* t, uint8_t* buf, const struct rdp_settings* set,
                            const char* password, struct rdp_mcs_result* r)
{
	struct rdp_stream s;
	size_t at, data;
	static const char* addr = "0.0.0.0";
	static const char* dir = "C:\\Windows\\System32\\mstscax.dll";
	int rc;
	tpkt_begin(&s, buf, BUF);
	rdp_s_u8(&s, MCS_SDRQ << 2);
	rdp_s_u16be(&s, (uint16_t)(r->user_channel - MCS_BASE_CHANNEL));
	rdp_s_u16be(&s, r->io_channel);
	rdp_s_u8(&s, 0x70);                  /* dataPriority high, segmentation begin|end */
	rdp_s_u16be(&s, 0);                  /* length, 2-byte PER form, patched below */
	at = s.pos;
	/* Basic Security Header: ENCRYPTION_LEVEL_NONE (2.2.8.1.1.2.1) */
	rdp_s_u16le(&s, SEC_INFO_PKT);
	rdp_s_u16le(&s, 0);
	/* TS_INFO_PACKET */
	rdp_s_u32le(&s, 0);                  /* CodePage */
	rdp_s_u32le(&s, 0x00000001 | 0x00000002 | 0x00000008 | 0x00000010 | 0x00000040 | 0x00000100 |
	                0x00010000);         /* MOUSE DISABLECTRLALTDEL AUTOLOGON UNICODE LOGONNOTIFY ENABLEWINDOWSKEY LOGONERRORS */
	rdp_s_u16le(&s, (uint16_t)rdp_utf16_len(set->domain));   /* bytes, the NUL not counted */
	rdp_s_u16le(&s, (uint16_t)rdp_utf16_len(set->user));
	rdp_s_u16le(&s, (uint16_t)rdp_utf16_len(password));
	rdp_s_u16le(&s, 0);                  /* cbAlternateShell */
	rdp_s_u16le(&s, 0);                  /* cbWorkingDir */
	utf16z(&s, set->domain);
	utf16z(&s, set->user);
	utf16z(&s, password);
	utf16z(&s, "");
	utf16z(&s, "");
	/* TS_EXTENDED_INFO_PACKET (2.2.1.11.1.1.1) */
	rdp_s_u16le(&s, 2);                  /* clientAddressFamily AF_INET */
	rdp_s_u16le(&s, (uint16_t)(2 * (strlen(addr) + 1)));
	utf16z(&s, addr);
	rdp_s_u16le(&s, (uint16_t)(2 * (strlen(dir) + 1)));
	utf16z(&s, dir);
	/* TS_TIME_ZONE_INFORMATION (2.2.1.11.1.1.1.1): UTC, no daylight time */
	rdp_s_u32le(&s, 0);                  /* Bias */
	utf16_fixed(&s, "GMT Standard Time", 64);
	rdp_s_bytes(&s, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16);
	rdp_s_u32le(&s, 0);
	utf16_fixed(&s, "GMT Daylight Time", 64);
	rdp_s_bytes(&s, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16);
	rdp_s_u32le(&s, 0);
	rdp_s_u32le(&s, 0);                  /* clientSessionId */
	rdp_s_u32le(&s, 0x00000007);         /* performanceFlags: no wallpaper, full-window drag, menu animations */
	rdp_s_u16le(&s, 0);                  /* cbAutoReconnectCookie */
	if (s.err)
		rc = RDP_E_SPACE;
	else
	{
		data = s.pos - at;
		s.p[at - 2] = (uint8_t)(0x80 | (data >> 8));
		s.p[at - 1] = (uint8_t)data;
		rc = tpkt_send(t, &s);
	}
	memset(buf, 0, s.pos);               /* it held the password */
	return rc;
}

/* ---- the first licensing PDU (2.2.1.12) ---- */

static int read_license(struct rdp_transport* t, uint8_t* buf, struct rdp_mcs_result* r)
{
	int tries;
	for (tries = 0; tries < 8; tries++)
	{
		struct rdp_stream s;
		unsigned choice = 0, unused = 0;
		size_t len;
		int rc = read_domain(t, buf, &s, r, &choice, &unused);
		if (rc < 0)
			return rc;
		if (choice != MCS_SDIN)
			continue;
		rdp_s_skip(&s, 4);               /* initiator, channelId */
		rdp_s_skip(&s, 1);               /* dataPriority, segmentation */
		len = per_get_len(&s);
		if (s.err || len < 4 || len > rdp_s_left(&s))
			return RDP_E_PROTO;
		r->sec_flags = rdp_s_get_u16le(&s);
		rdp_s_skip(&s, 2);
		if (r->sec_flags & SEC_ENCRYPT)  /* standard RDP security: not with TLS (5.4) */
			return RDP_E_PROTO;
		if (r->sec_flags & SEC_LICENSE_PKT)
		{
			r->lic_type = rdp_s_get_u8(&s);
			r->lic_flags = rdp_s_get_u8(&s);
			r->lic_size = rdp_s_get_u16le(&s);
			if (r->lic_type == 0xFF)     /* ERROR_ALERT: LICENSE_ERROR_MESSAGE */
			{
				r->lic_error = rdp_s_get_u32le(&s);
				r->lic_state = rdp_s_get_u32le(&s);
			}
			if (s.err)
				return RDP_E_PROTO;
			/* [MS-RDPBCGR] 1.3.1.1 phase 8, [MS-RDPELE] 1.3.3: a server with
			 * no licensing to do (GNOME Remote Desktop, xrdp, Windows in
			 * its grace period with "valid client" on) ends it at once with
			 * STATUS_VALID_CLIENT and ST_NO_TRANSITION, and Demand Active
			 * follows. A LICENSE_REQUEST starts the exchange of [MS-RDPELE]
			 * 3.2 (New License Request: a premaster secret under the
			 * server's RSA key, then the Platform Challenge Response), which
			 * this client does not do yet: RDP_E_LICENSE, said plainly,
			 * rather than waiting for a Demand Active that cannot come. */
			if (r->lic_type == 0xFF && r->lic_error == 0x07)
				return 0;
			return RDP_E_LICENSE;
		}
		/* a Set Error Info PDU ahead of a disconnection (2.2.5.1.1):
		 * TS_SHARECONTROLHEADER, TS_SHAREDATAHEADER, errorInfo */
		if (len >= 4 + 6 + 12 + 4)
		{
			uint16_t type;
			rdp_s_skip(&s, 2);           /* totalLength */
			type = rdp_s_get_u16le(&s);
			rdp_s_skip(&s, 2 + 4 + 1 + 1 + 2);   /* pduSource, shareId, pad1, streamId, uncompressedLength */
			if ((type & 0x0f) == 7 && rdp_s_get_u8(&s) == 0x2f)   /* PDUTYPE_DATAPDU, PDUTYPE2_SET_ERROR_INFO_PDU */
			{
				rdp_s_skip(&s, 3);
				r->error_info = rdp_s_get_u32le(&s);
			}
		}
	}
	return RDP_E_PROTO;
}

/* ---- after licensing: the session's PDUs ---- */

int rdp_mcs_send_data(struct rdp_transport* t, uint8_t* buf, size_t size, uint16_t user, uint16_t channel,
                      const uint8_t* data, size_t len)
{
	struct rdp_stream s;
	tpkt_begin(&s, buf, size);
	rdp_s_u8(&s, MCS_SDRQ << 2);
	rdp_s_u16be(&s, (uint16_t)(user - MCS_BASE_CHANNEL));
	rdp_s_u16be(&s, channel);
	rdp_s_u8(&s, 0x70);                  /* dataPriority high, segmentation begin|end */
	per_len(&s, len);
	rdp_s_bytes(&s, data, len);
	return tpkt_send(t, &s);
}

/* one PDU of either kind ([MS-RDPBCGR] 2.2.9.1.2 fast-path, or a TPKT):
 * the first byte's action bits tell them apart (5.3.8, 2.2.9.1.2) */
int rdp_pdu_read(struct rdp_transport* t, uint8_t* buf, size_t size, struct rdp_mcs_result* r, struct rdp_pdu* pdu)
{
	size_t len, head;
	int rc;
	memset(pdu, 0, sizeof(*pdu));
	if (size < 8)
		return RDP_E_SPACE;
	rc = rdp_transport_read_full(t, buf, 2);
	if (rc < 0)
		return rc;
	if ((buf[0] & 3) == 0)
	{
		/* fpOutputHeader, then a length in one or two bytes (2.2.9.1.2) */
		head = 2;
		len = buf[1];
		if (len & 0x80)
		{
			rc = rdp_transport_read_full(t, buf + 2, 1);
			if (rc < 0)
				return rc;
			len = ((len & 0x7f) << 8) | buf[2];
			head = 3;
		}
		if (buf[0] & 0x80)               /* FASTPATH_OUTPUT_ENCRYPTED: standard RDP security only */
			return RDP_E_PROTO;
		if (len < head || len > size)
			return len > size ? RDP_E_SPACE : RDP_E_PROTO;
		rc = rdp_transport_read_full(t, buf + head, len - head);
		if (rc < 0)
			return rc;
		pdu->fastpath = 1;
		pdu->data = buf + head;
		pdu->len = len - head;
		return 0;
	}
	if (buf[0] != RDP_TPKT_VERSION)
		return RDP_E_PROTO;
	rc = rdp_transport_read_full(t, buf + 2, 2);
	if (rc < 0)
		return rc;
	len = ((size_t)buf[2] << 8) | buf[3];
	if (len < RDP_TPKT_HEADER + 3 || len > size)
		return len > size ? RDP_E_SPACE : RDP_E_PROTO;
	rc = rdp_transport_read_full(t, buf + 4, len - 4);
	if (rc < 0)
		return rc;
	{
		struct rdp_stream s;
		unsigned choice;
		uint8_t b;
		rc = tpkt_data(buf, len, &s);
		if (rc < 0)
			return rc;
		b = rdp_s_get_u8(&s);
		choice = b >> 2;
		pdu->choice = (uint8_t)choice;
		pdu->data = buf;
		pdu->len = 0;
		if (choice == MCS_DPUM)
		{
			uint8_t c = rdp_s_get_u8(&s);
			r->disconnect_reason = 1 + (((b & 3u) << 1) | (c >> 7));
			return AMIPR_ECLOSED;
		}
		if (choice != MCS_SDIN)
			return 0;                    /* nothing for the session */
		rdp_s_skip(&s, 2);               /* initiator */
		pdu->channel = rdp_s_get_u16be(&s);
		rdp_s_skip(&s, 1);               /* dataPriority, segmentation */
		len = per_get_len(&s);
		if (s.err || len > rdp_s_left(&s))
			return RDP_E_PROTO;
		pdu->data = s.p + s.pos;
		pdu->len = len;
	}
	return 0;
}

int rdp_mcs_disconnect(struct rdp_transport* t)
{
	/* Disconnect Provider Ultimatum, reason rn-user-requested (3): the
	 * 3-bit reason straddles the first two bytes (T.125 ALIGNED PER) */
	uint8_t buf[16];
	static const uint8_t dpum[2] = { (MCS_DPUM << 2) | (3 >> 1), (3 & 1) << 7 };
	struct rdp_stream s;
	tpkt_begin(&s, buf, sizeof(buf));
	rdp_s_bytes(&s, dpum, 2);
	return tpkt_send(t, &s);
}

int rdp_mcs_connect(struct rdp_transport* t, const struct rdp_settings* s, uint32_t selected,
                    const char* password, struct rdp_mcs_result* r)
{
	static const uint8_t erect[5] = { MCS_EDRQ << 2, 0x01, 0x00, 0x01, 0x00 };   /* subHeight 0, subInterval 0 */
	static const uint8_t attach[1] = { MCS_AURQ << 2 };
	struct rdp_stream st;
	unsigned choice = 0;
	uint8_t* buf;
	int rc, i;

	memset(r, 0, sizeof(*r));
	buf = AllocVec(BUF, MEMF_ANY | MEMF_CLEAR);
	if (!buf)
		return AMIPR_ENOMEM;
	rc = send_connect_initial(t, buf, s, selected);
	if (rc < 0)
		goto done;
	r->stage = RDP_MCS_ST_CONNECT_SENT;
	rc = read_connect_response(t, buf, r);
	if (rc < 0)
		goto done;
	r->stage = RDP_MCS_ST_CONNECTED;
	if (r->enc_level != 0)
	{
		/* standard RDP security inside TLS is not what this client does */
		rc = RDP_E_PROTO;
		goto done;
	}
	rc = send_domain(t, buf, erect, sizeof(erect));
	if (rc >= 0)
		rc = send_domain(t, buf, attach, sizeof(attach));
	if (rc >= 0)
		rc = read_domain(t, buf, &st, r, &choice, &r->attach_result);
	if (rc < 0)
		goto done;
	if (choice != MCS_AUCF)
	{
		rc = RDP_E_PROTO;
		goto done;
	}
	r->user_channel = (uint16_t)(MCS_BASE_CHANNEL + rdp_s_get_u16be(&st));
	if (st.err)
	{
		rc = RDP_E_PROTO;
		goto done;
	}
	if (r->attach_result != 0)
	{
		rc = RDP_E_MCS;
		goto done;
	}
	r->stage = RDP_MCS_ST_ATTACHED;
	/* the user channel, the I/O channel, the message channel, then the static channels (2.2.1.8) */
	rc = join(t, buf, r, r->user_channel);
	if (rc >= 0)
		rc = join(t, buf, r, r->io_channel);
	if (rc >= 0 && r->msg_channel)
		rc = join(t, buf, r, r->msg_channel);
	for (i = 0; rc >= 0 && i < r->channel_count; i++)
		if (r->channels[i])
			rc = join(t, buf, r, r->channels[i]);
	if (rc < 0)
		goto done;
	r->stage = RDP_MCS_ST_JOINED;
	rc = send_client_info(t, buf, s, password, r);
	if (rc < 0)
		goto done;
	r->stage = RDP_MCS_ST_INFO_SENT;
	rc = read_license(t, buf, r);
	if (rc == RDP_E_LICENSE)
		r->stage = RDP_MCS_ST_LICENSING;   /* reached, and it says why it stops (lic_type, lic_error) */
	if (rc < 0)
		goto done;
	r->stage = RDP_MCS_ST_LICENSING;
done:
	memset(buf, 0, BUF);
	FreeVec(buf);
	return rc < 0 ? rc : 0;
}

const char* rdp_mcs_stage_name(int stage)
{
	switch (stage)
	{
		case RDP_MCS_ST_CONNECT_SENT: return "the MCS Connect Initial";
		case RDP_MCS_ST_CONNECTED: return "the MCS Connect Response";
		case RDP_MCS_ST_ATTACHED: return "Attach User";
		case RDP_MCS_ST_JOINED: return "Channel Join";
		case RDP_MCS_ST_INFO_SENT: return "the Client Info PDU";
		case RDP_MCS_ST_LICENSING: return "licensing";
		default: return "the start";
	}
}

/* [MS-RDPELE] 2.2.1.12.1.3 dwErrorCode */
const char* rdp_license_error_text(uint32_t code)
{
	switch (code)
	{
		case 0x01: return "ERR_INVALID_SERVER_CERTIFICATE";
		case 0x02: return "ERR_NO_LICENSE";
		case 0x04: return "ERR_INVALID_SCOPE";
		case 0x06: return "ERR_NO_LICENSE_SERVER";
		case 0x07: return "STATUS_VALID_CLIENT";
		case 0x08: return "ERR_INVALID_CLIENT";
		case 0x0B: return "ERR_INVALID_PRODUCTID";
		case 0x0C: return "ERR_INVALID_MESSAGE_LEN";
		case 0x03: return "ERR_INVALID_MAC";
		default: return "?";
	}
}
