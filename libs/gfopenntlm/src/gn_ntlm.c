/* gfopenntlm: the NTLMv2 client, written from [MS-NLMP] (v20260330):
 * 2.2.1 messages, 2.2.2 structures, 3.1.5.1 client, 3.3.2 NTLMv2
 * responses, 3.4 session security, 3.4.5 key derivation.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gn_internal.h"
#include <string.h>

static const uint8_t sig_ntlmssp[8] = { 'N', 'T', 'L', 'M', 'S', 'S', 'P', 0 };

static const char c2s_sign[] = "session key to client-to-server signing key magic constant";
static const char s2c_sign[] = "session key to server-to-client signing key magic constant";
static const char c2s_seal[] = "session key to client-to-server sealing key magic constant";
static const char s2c_seal[] = "session key to server-to-client sealing key magic constant";

/* the NTLMSSP Version field (2.2.2.10): 10.0 build 19041, NTLM revision 15 */
static const uint8_t default_version[8] = { 10, 0, 0x61, 0x4a, 0, 0, 0, 0x0f };

static uint8_t *dup_utf16(const char *s, int up, size_t *len)
{
	return gn_utf16(s, up, len);
}

void gn_ntowfv2(const uint8_t nt_hash[16], const uint8_t *user16, size_t ulen,
                const uint8_t *dom16, size_t dlen, uint8_t out[16])
{
	struct gn_hmac_md5 h;
	gn_hmac_md5_init(&h, nt_hash, 16);
	gn_hmac_md5_update(&h, user16, ulen);
	gn_hmac_md5_update(&h, dom16, dlen);
	gn_hmac_md5_final(&h, out);
}

int gn_ntlm_init(struct gn_ntlm *n, const char *user, const char *domain,
                 const char *password, const char *workstation, const char *spn)
{
	uint8_t *pw, *uu;
	size_t pwl, uul;
	memset(n, 0, sizeof *n);
	n->user = dup_utf16(user, 0, &n->user_len);
	n->domain = dup_utf16(domain, 0, &n->domain_len);
	n->ws = dup_utf16(workstation, 0, &n->ws_len);
	n->spn = spn ? dup_utf16(spn, 0, &n->spn_len) : NULL;
	pw = dup_utf16(password, 0, &pwl);
	uu = dup_utf16(user, 1, &uul);
	if (!n->user || !n->domain || !n->ws || (spn && !n->spn) || !pw || !uu) {
		if (pw) gn_free(pw, pwl);
		if (uu) gn_free(uu, uul);
		gn_ntlm_clear(n);
		return GN_ENOMEM;
	}
	/* NTOWFv2(Passwd, User, UserDom) =
	 *   HMAC_MD5(MD4(UNICODE(Passwd)), UNICODE(ConcatenationOf(Uppercase(User), UserDom))) */
	gn_md4(pw, pwl, n->nt_hash);
	gn_ntowfv2(n->nt_hash, uu, uul, n->domain, n->domain_len, n->response_key);
	gn_free(pw, pwl);
	gn_free(uu, uul);
	n->client_flags = GN_NTLM_CLIENT_FLAGS;
	memcpy(n->version, default_version, 8);
	return GN_OK;
}

void gn_ntlm_clear(struct gn_ntlm *n)
{
	gn_free(n->user, n->user_len + 2);
	gn_free(n->domain, n->domain_len + 2);
	gn_free(n->ws, n->ws_len + 2);
	gn_free(n->spn, n->spn_len + 2);
	gn_free(n->negotiate, n->negotiate_len);
	gn_wipe(n, sizeof *n);
}

/* NEGOTIATE_MESSAGE (2.2.1.1): no domain or workstation is supplied. */
int gn_ntlm_negotiate(struct gn_ntlm *n, uint8_t *out, size_t size, size_t *outlen)
{
	uint8_t m[40];
	if (n->state != 0) return GN_ESTATE;
	memset(m, 0, sizeof m);
	memcpy(m, sig_ntlmssp, 8);
	gn_put32(m + 8, 1);
	gn_put32(m + 12, n->client_flags);
	/* DomainNameFields (16) and WorkstationFields (24) zero: offsets point
	 * past the header, as 2.2.1.1 allows for empty fields */
	gn_put32(m + 20, 40);
	gn_put32(m + 28, 40);
	memcpy(m + 32, n->version, 8);
	if (size < sizeof m) return GN_EBUFFER;
	n->negotiate = gn_alloc(sizeof m);
	if (!n->negotiate) return GN_ENOMEM;
	memcpy(n->negotiate, m, sizeof m);
	n->negotiate_len = sizeof m;
	memcpy(out, m, sizeof m);
	*outlen = sizeof m;
	n->state = 1;
	return GN_OK;
}

static int get_field(const uint8_t *msg, size_t len, size_t at, const uint8_t **p, size_t *flen)
{
	uint32_t l = gn_get16(msg + at), off = gn_get32(msg + at + 4);
	if (l == 0) { *p = msg; *flen = 0; return 0; }
	if (off > len || l > len - off) return -1;
	*p = msg + off;
	*flen = l;
	return 0;
}

void gn_ntlm_set_keys(struct gn_ntlm *n, const uint8_t exported[16], uint32_t flags, int server)
{
	struct gn_md5 c;
	size_t klen = 16;
	uint8_t c2s[16], s2c[16];
	memcpy(n->exported_key, exported, 16);
	n->flags = flags;
	/* SIGNKEY (3.4.5.2): MD5(ConcatenationOf(ExportedSessionKey, magic incl. its NUL)) */
	gn_md5_init(&c); gn_md5_update(&c, exported, 16);
	gn_md5_update(&c, c2s_sign, sizeof c2s_sign); gn_md5_final(&c, c2s);
	gn_md5_init(&c); gn_md5_update(&c, exported, 16);
	gn_md5_update(&c, s2c_sign, sizeof s2c_sign); gn_md5_final(&c, s2c);
	/* the sending direction's key in client_sign, the receiving one's in
	 * server_sign (swapped when a test plays the server) */
	memcpy(n->client_sign, server ? s2c : c2s, 16);
	memcpy(n->server_sign, server ? c2s : s2c, 16);
	/* SEALKEY (3.4.5.3): 128-bit, 56-bit (7 bytes) or 40-bit (5 bytes) */
	if (!(flags & NTLMSSP_NEGOTIATE_128))
		klen = (flags & NTLMSSP_NEGOTIATE_56) ? 7 : 5;
	gn_md5_init(&c); gn_md5_update(&c, exported, klen);
	gn_md5_update(&c, c2s_seal, sizeof c2s_seal); gn_md5_final(&c, n->client_seal_key);
	gn_md5_init(&c); gn_md5_update(&c, exported, klen);
	gn_md5_update(&c, s2c_seal, sizeof s2c_seal); gn_md5_final(&c, n->server_seal_key);
	/* one RC4 handle per direction, kept for the whole connection (3.4.3) */
	gn_rc4_init(&n->client_seal, server ? n->server_seal_key : n->client_seal_key, 16);
	gn_rc4_init(&n->server_seal, server ? n->client_seal_key : n->server_seal_key, 16);
	n->send_seq = n->recv_seq = 0;
	n->state = 2;
	gn_wipe(c2s, 16); gn_wipe(s2c, 16);
}

/* AUTHENTICATE_MESSAGE (2.2.1.3) for the CHALLENGE_MESSAGE (2.2.1.2). */
int gn_ntlm_authenticate(struct gn_ntlm *n, const uint8_t *chal, size_t clen,
                         uint8_t *out, size_t size, size_t *outlen)
{
	const uint8_t *ti, *tn;
	uint8_t sc[8], cc[8], proof[16], kx[16], exported[16], enckey[16], lm[24];
	uint8_t *temp = NULL, *m;
	size_t tilen, tnlen, temp_len, nt_len, hdr, total, off, i;
	uint32_t cflags, flags;
	uint64_t now = 0;
	int have_time = 0, mic, raw = (n->test & GN_NTLM_T_RAW) != 0;
	struct gn_hmac_md5 h;

	if (n->state != 1) return GN_ESTATE;
	if (clen < 48 || memcmp(chal, sig_ntlmssp, 8) || gn_get32(chal + 8) != 2)
		return GN_EPROTOCOL;
	cflags = gn_get32(chal + 20);
	memcpy(sc, chal + 24, 8);
	if (get_field(chal, clen, 12, &tn, &tnlen) || get_field(chal, clen, 40, &ti, &tilen))
		return GN_EPROTOCOL;
	(void)tn; (void)tnlen;
	if (clen >= 56 && (cflags & NTLMSSP_NEGOTIATE_VERSION))
		n->peer_version = gn_get32(chal + 48);
	if (!(cflags & NTLMSSP_NEGOTIATE_UNICODE)) return GN_EUNSUPPORTED;
	if (!(cflags & NTLMSSP_NEGOTIATE_TARGET_INFO) || tilen < 4) return GN_EUNSUPPORTED; /* NTLMv2 needs it */

	/* walk the server's AV pairs (2.2.2.1) */
	for (i = 0; ; ) {
		uint16_t id, l;
		if (i + 4 > tilen) return GN_EPROTOCOL;
		id = gn_get16(ti + i); l = gn_get16(ti + i + 2);
		if (i + 4 + l > tilen) return GN_EPROTOCOL;
		if (id == MsvAvEOL) break;
		if (id == MsvAvTimestamp && l == 8) {
			now = (uint64_t)gn_get32(ti + i + 4) | ((uint64_t)gn_get32(ti + i + 8) << 32);
			have_time = 1;
		}
		i += 4 + l;
	}
	if (n->test & GN_NTLM_T_FIXED) {
		now = n->t_time;
		memcpy(cc, n->t_client_challenge, 8);
	} else {
		if (!have_time) now = gn_filetime_now();
		if (gn_random(cc, 8)) return GN_ENORANDOM;
	}
	mic = !raw;

	/* temp (3.3.2): Responserversion, HiResponserversion, Z(6), Time,
	 * ClientChallenge, Z(4), ServerName (the AV pairs), Z(4) */
	{
		size_t avlen = raw ? tilen : 0;
		if (!raw) {
			/* the server's pairs without its EOL (and without a flags pair,
			 * which is put back with the MIC bit), then MsvAvFlags,
			 * MsvAvTargetName, MsvChannelBindings (no TLS channel bindings:
			 * Z(16)) and MsvAvEOL (3.1.5.1.2) */
			avlen = i + 8 + 4 + (n->spn ? 4 + n->spn_len : 0) + 4 + 16 + 4;
		}
		temp_len = 28 + avlen + 4;
		temp = gn_alloc(temp_len);
		if (!temp) return GN_ENOMEM;
		temp[0] = 1; temp[1] = 1;
		gn_put32(temp + 8, (uint32_t)now);
		gn_put32(temp + 12, (uint32_t)(now >> 32));
		memcpy(temp + 16, cc, 8);
		if (raw) memcpy(temp + 28, ti, tilen);
		else {
			uint8_t *q = temp + 28;
			size_t j;
			uint32_t avflags = 0x2;       /* the MIC is present */
			for (j = 0; j < i; ) {
				uint16_t id = gn_get16(ti + j), l = gn_get16(ti + j + 2);
				if (id == MsvAvFlags && l == 4) avflags |= gn_get32(ti + j + 4);
				else if (id != MsvAvTargetName && id != MsvChannelBindings) {
					memcpy(q, ti + j, 4u + l); q += 4u + l;
				}
				j += 4u + l;
			}
			gn_put16(q, MsvAvFlags); gn_put16(q + 2, 4); gn_put32(q + 4, avflags); q += 8;
			if (n->spn) {
				gn_put16(q, MsvAvTargetName); gn_put16(q + 2, (uint16_t)n->spn_len);
				memcpy(q + 4, n->spn, n->spn_len); q += 4 + n->spn_len;
			}
			gn_put16(q, MsvChannelBindings); gn_put16(q + 2, 16); q += 4 + 16;
			gn_put16(q, MsvAvEOL); gn_put16(q + 2, 0); q += 4;
			temp_len = (size_t)(q - temp) + 4;
		}
	}
	/* NTProofStr = HMAC_MD5(ResponseKeyNT, ConcatenationOf(CHALLENGE_MESSAGE.ServerChallenge, temp)) */
	gn_hmac_md5_init(&h, n->response_key, 16);
	gn_hmac_md5_update(&h, sc, 8);
	gn_hmac_md5_update(&h, temp, temp_len);
	gn_hmac_md5_final(&h, proof);
	nt_len = 16 + temp_len;
	/* LmChallengeResponse: with MsvAvTimestamp the client sends Z(24)
	 * (3.1.5.1.2); otherwise LMv2 = HMAC_MD5(ResponseKeyLM, SC + CC) + CC */
	if (have_time && !raw) memset(lm, 0, 24);
	else {
		gn_hmac_md5_init(&h, n->response_key, 16);
		gn_hmac_md5_update(&h, sc, 8);
		gn_hmac_md5_update(&h, cc, 8);
		gn_hmac_md5_final(&h, lm);
		memcpy(lm + 16, cc, 8);
	}
	/* SessionBaseKey = HMAC_MD5(ResponseKeyNT, NTProofStr); for NTLMv2
	 * KeyExchangeKey is SessionBaseKey (3.4.5.1) */
	gn_hmac_md5(n->response_key, 16, proof, 16, n->session_base_key);
	memcpy(kx, n->session_base_key, 16);

	flags = (cflags & n->client_flags) | (n->client_flags & NTLMSSP_REQUEST_TARGET);
	if (flags & NTLMSSP_NEGOTIATE_KEY_EXCH) {
		struct gn_rc4 r;
		if (n->test & GN_NTLM_T_FIXED) memcpy(exported, n->t_session_key, 16);
		else if (gn_random(exported, 16)) { gn_free(temp, temp_len); return GN_ENORANDOM; }
		gn_rc4_init(&r, kx, 16);
		gn_rc4_crypt(&r, exported, enckey, 16);
		gn_wipe(&r, sizeof r);
	} else
		memcpy(exported, kx, 16);

	hdr = mic ? 88 : 72;
	total = hdr + n->domain_len + n->user_len + n->ws_len + 24 + nt_len +
	        ((flags & NTLMSSP_NEGOTIATE_KEY_EXCH) ? 16 : 0);
	if (size < total) { gn_free(temp, temp_len); return GN_EBUFFER; }
	m = out;
	memset(m, 0, hdr);
	memcpy(m, sig_ntlmssp, 8);
	gn_put32(m + 8, 3);
	off = hdr;
#define FIELD(at, src, len) do { gn_put16(m + (at), (uint16_t)(len)); gn_put16(m + (at) + 2, (uint16_t)(len)); \
	gn_put32(m + (at) + 4, (uint32_t)off); if (len) memcpy(m + off, (src), (len)); off += (len); } while (0)
	/* the payload in the order of the specification's example (4.2.4.3) */
	FIELD(28, n->domain, n->domain_len);
	FIELD(36, n->user, n->user_len);
	FIELD(44, n->ws, n->ws_len);
	FIELD(12, lm, 24);
	gn_put16(m + 20, (uint16_t)nt_len); gn_put16(m + 22, (uint16_t)nt_len); gn_put32(m + 24, (uint32_t)off);
	memcpy(m + off, proof, 16); memcpy(m + off + 16, temp, temp_len); off += nt_len;
	if (flags & NTLMSSP_NEGOTIATE_KEY_EXCH) FIELD(52, enckey, 16);
	else gn_put32(m + 56, (uint32_t)off);
#undef FIELD
	gn_put32(m + 60, flags);
	memcpy(m + 64, n->version, 8);
	if (mic) {
		/* MIC = HMAC_MD5(ExportedSessionKey, NEGOTIATE + CHALLENGE + AUTHENTICATE
		 * with the MIC field zero) (3.1.5.1.2) */
		uint8_t mv[16];
		gn_hmac_md5_init(&h, exported, 16);
		gn_hmac_md5_update(&h, n->negotiate, n->negotiate_len);
		gn_hmac_md5_update(&h, chal, clen);
		gn_hmac_md5_update(&h, m, total);
		gn_hmac_md5_final(&h, mv);
		memcpy(m + 72, mv, 16);
	}
	*outlen = total;
	gn_ntlm_set_keys(n, exported, flags, 0);
	gn_free(temp, temp_len);
	gn_wipe(exported, 16); gn_wipe(kx, 16); gn_wipe(proof, 16);
	return GN_OK;
}

/* MAC with extended session security (3.4.4.2), sealing first (3.4.3). */
static void mac(const uint8_t sign[16], struct gn_rc4 *rc4, uint32_t flags, uint32_t seq,
                const uint8_t *msg, size_t len, uint8_t sig[16])
{
	struct gn_hmac_md5 h;
	uint8_t s4[4], d[16];
	gn_put32(s4, seq);
	gn_hmac_md5_init(&h, sign, 16);
	gn_hmac_md5_update(&h, s4, 4);
	gn_hmac_md5_update(&h, msg, len);
	gn_hmac_md5_final(&h, d);
	gn_put32(sig, 1);
	if (flags & NTLMSSP_NEGOTIATE_KEY_EXCH) gn_rc4_crypt(rc4, d, sig + 4, 8);
	else memcpy(sig + 4, d, 8);
	gn_put32(sig + 12, seq);
}

/* GSS_WrapEx (3.4.6): the 16-byte signature, then the sealed message. */
int gn_ntlm_wrap(struct gn_ntlm *n, const uint8_t *in, size_t len,
                 uint8_t *out, size_t size, size_t *outlen)
{
	if (n->state != 2) return GN_ESTATE;
	if (!(n->flags & NTLMSSP_NEGOTIATE_EXTENDED_SESSIONSECURITY)) return GN_EUNSUPPORTED;
	if (size < len + 16) return GN_EBUFFER;
	gn_rc4_crypt(&n->client_seal, in, out + 16, len);
	mac(n->client_sign, &n->client_seal, n->flags, n->send_seq, in, len, out);
	n->send_seq++;
	*outlen = len + 16;
	return GN_OK;
}

/* GSS_UnwrapEx (3.4.7) */
int gn_ntlm_unwrap(struct gn_ntlm *n, const uint8_t *in, size_t len,
                   uint8_t *out, size_t size, size_t *outlen)
{
	uint8_t sig[16];
	size_t i;
	unsigned diff = 0;
	if (n->state != 2) return GN_ESTATE;
	if (len < 16) return GN_EPROTOCOL;
	if (size < len - 16) return GN_EBUFFER;
	gn_rc4_crypt(&n->server_seal, in + 16, out, len - 16);
	mac(n->server_sign, &n->server_seal, n->flags, n->recv_seq, out, len - 16, sig);
	for (i = 0; i < 16; i++) diff |= sig[i] ^ in[i];
	if (diff) { gn_wipe(out, len - 16); return GN_EINTEGRITY; }
	n->recv_seq++;
	*outlen = len - 16;
	return GN_OK;
}
