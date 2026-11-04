/* gfopenntlm: the CredSSP client, written from [MS-CSSP] (v20240423):
 * 2.2.1 TSRequest, 2.2.1.2 TSCredentials, 2.2.1.2.1 TSPasswordCreds and
 * 3.1.5's sequence, with NTLM ([MS-NLMP]) as the authentication package.
 * DER per ITU-T X.690. MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gn_internal.h"
#include <string.h>

static const char c2s_magic[] = "CredSSP Client-To-Server Binding Hash";
static const char s2c_magic[] = "CredSSP Server-To-Client Binding Hash";

static size_t tl(size_t n) { return gn_der_hdr_size(n) + n; }

/* the contents octets of a 32-bit INTEGER (8.3): two's complement, minimal */
static size_t int_bytes(int32_t v, uint8_t b[5])
{
	uint8_t t[4];
	size_t n = 4, i = 0;
	t[0] = (uint8_t)((uint32_t)v >> 24); t[1] = (uint8_t)((uint32_t)v >> 16);
	t[2] = (uint8_t)((uint32_t)v >> 8); t[3] = (uint8_t)v;
	while (n > 1 && ((t[i] == 0 && !(t[i + 1] & 0x80)) || (t[i] == 0xff && (t[i + 1] & 0x80)))) {
		i++; n--;
	}
	memcpy(b, t + i, n);
	return n;
}

static uint8_t *put_hdr(uint8_t *w, uint8_t tag, size_t len)
{
	return w + gn_der_hdr(w, tag, len);
}

static uint8_t *put_octets(uint8_t *w, uint8_t ctx, const uint8_t *p, size_t len)
{
	w = put_hdr(w, ctx, tl(len));
	w = put_hdr(w, 0x04, len);
	if (len) memcpy(w, p, len);
	return w + len;
}

static uint8_t *put_int(uint8_t *w, uint8_t ctx, int32_t v)
{
	uint8_t b[5];
	size_t n = int_bytes(v, b);
	w = put_hdr(w, ctx, tl(n));
	w = put_hdr(w, 0x02, n);
	memcpy(w, b, n);
	return w + n;
}

size_t gn_tsrequest_encode(const struct gn_tsrequest *r, uint8_t *out, size_t size)
{
	uint8_t b[5];
	size_t body = tl(tl(int_bytes((int32_t)r->version, b))), total;
	uint8_t *w;
	if (r->nego_token) body += tl(tl(tl(tl(tl(r->nego_token_len)))));
	if (r->auth_info) body += tl(tl(r->auth_info_len));
	if (r->pub_key_auth) body += tl(tl(r->pub_key_auth_len));
	if (r->has_error) body += tl(tl(int_bytes((int32_t)r->error_code, b)));
	if (r->client_nonce) body += tl(tl(r->client_nonce_len));
	total = tl(body);
	if (!out || size < total) return total;
	w = put_hdr(out, 0x30, body);
	w = put_int(w, 0xa0, (int32_t)r->version);
	if (r->nego_token) {
		size_t t = r->nego_token_len;
		/* negoTokens [1] NegoData ::= SEQUENCE OF SEQUENCE { negoToken [0] OCTET STRING } */
		w = put_hdr(w, 0xa1, tl(tl(tl(tl(t)))));
		w = put_hdr(w, 0x30, tl(tl(tl(t))));
		w = put_hdr(w, 0x30, tl(tl(t)));
		w = put_octets(w, 0xa0, r->nego_token, t);
	}
	if (r->auth_info) w = put_octets(w, 0xa2, r->auth_info, r->auth_info_len);
	if (r->pub_key_auth) w = put_octets(w, 0xa3, r->pub_key_auth, r->pub_key_auth_len);
	if (r->has_error) w = put_int(w, 0xa4, (int32_t)r->error_code);
	if (r->client_nonce) w = put_octets(w, 0xa5, r->client_nonce, r->client_nonce_len);
	return (size_t)(w - out);
}

static int get_int(struct gn_der *v, uint32_t *out)
{
	struct gn_der i;
	size_t k;
	uint32_t x;
	if (gn_der_get(v, 0x02, &i) || i.len < 1 || i.len > 5) return -1;
	x = (i.p[0] & 0x80) ? 0xffffffffu : 0;
	for (k = 0; k < i.len; k++) x = (x << 8) | i.p[k];
	*out = x;
	return 0;
}

static int get_octets(struct gn_der *v, const uint8_t **p, size_t *len)
{
	struct gn_der o;
	if (gn_der_get(v, 0x04, &o)) return -1;
	*p = o.p;
	*len = o.len;
	return 0;
}

int gn_tsrequest_decode(const uint8_t *in, size_t len, struct gn_tsrequest *r)
{
	struct gn_der d = { in, len }, seq, f;
	memset(r, 0, sizeof *r);
	if (gn_der_get(&d, 0x30, &seq)) return GN_EPROTOCOL;
	while (seq.len) {
		int tag = gn_der_peek(&seq);
		if (gn_der_get(&seq, (uint8_t)tag, &f)) return GN_EPROTOCOL;
		switch (tag) {
		case 0xa0: if (get_int(&f, &r->version)) return GN_EPROTOCOL; break;
		case 0xa1: {
			/* the first token of NegoData; NTLM carries one per message */
			struct gn_der so, s, t;
			if (gn_der_get(&f, 0x30, &so) || gn_der_get(&so, 0x30, &s) ||
			    gn_der_get(&s, 0xa0, &t) || get_octets(&t, &r->nego_token, &r->nego_token_len))
				return GN_EPROTOCOL;
			break;
		}
		case 0xa2: if (get_octets(&f, &r->auth_info, &r->auth_info_len)) return GN_EPROTOCOL; break;
		case 0xa3: if (get_octets(&f, &r->pub_key_auth, &r->pub_key_auth_len)) return GN_EPROTOCOL; break;
		case 0xa4: if (get_int(&f, &r->error_code)) return GN_EPROTOCOL; r->has_error = 1; break;
		case 0xa5: if (get_octets(&f, &r->client_nonce, &r->client_nonce_len)) return GN_EPROTOCOL; break;
		default: break;  /* a later version's field: skipped */
		}
	}
	return GN_OK;
}

size_t gn_tscredentials_encode(const uint8_t *dom, size_t dl, const uint8_t *user, size_t ul,
                               const uint8_t *pass, size_t pl, uint8_t *out, size_t size)
{
	size_t pc = tl(tl(dl)) + tl(tl(ul)) + tl(tl(pl));
	size_t pw = tl(pc);
	size_t body = tl(tl(1)) + tl(tl(pw));
	size_t total = tl(body);
	uint8_t *w;
	if (!out || size < total) return total;
	/* TSCredentials { credType [0] INTEGER (1), credentials [1] OCTET STRING } */
	w = put_hdr(out, 0x30, body);
	w = put_int(w, 0xa0, 1);
	w = put_hdr(w, 0xa1, tl(pw));
	w = put_hdr(w, 0x04, pw);
	/* TSPasswordCreds { domainName [0], userName [1], password [2] } */
	w = put_hdr(w, 0x30, pc);
	w = put_octets(w, 0xa0, dom, dl);
	w = put_octets(w, 0xa1, user, ul);
	w = put_octets(w, 0xa2, pass, pl);
	return (size_t)(w - out);
}

/* SHA256(magic incl. its NUL, Nonce, SubjectPublicKey) (3.1.5 steps 3, 4) */
void gn_cssp_hash(const char *magic, const uint8_t nonce[32], const uint8_t *pubkey,
                  size_t pklen, uint8_t out[32])
{
	struct gn_sha256 c;
	gn_sha256_init(&c);
	gn_sha256_update(&c, magic, strlen(magic) + 1);
	gn_sha256_update(&c, nonce, 32);
	gn_sha256_update(&c, pubkey, pklen);
	gn_sha256_final(&c, out);
}

int gn_cssp_init(struct gn_cssp *c, const char *user, const char *domain, const char *password,
                 const char *workstation, const char *server, const uint8_t *pubkey,
                 size_t pubkey_len, uint32_t max_version)
{
	char *spn = NULL;
	int e;
	memset(c, 0, sizeof *c);
	if (!pubkey || !pubkey_len) return GN_EBADARGS;
	if (server && *server) {
		size_t l = strlen(server);
		spn = gn_alloc(l + 9);
		if (!spn) return GN_ENOMEM;
		memcpy(spn, "TERMSRV/", 8);
		memcpy(spn + 8, server, l + 1);
	}
	e = gn_ntlm_init(&c->ntlm, user, domain, password, workstation, spn);
	if (spn) gn_free(spn, strlen(spn) + 1);
	if (e) { memset(c, 0, sizeof *c); return e; }
	c->user16 = gn_utf16(user, 0, &c->user16_len);
	c->domain16 = gn_utf16(domain, 0, &c->domain16_len);
	c->pass16 = gn_utf16(password, 0, &c->pass16_len);
	c->pubkey = gn_alloc(pubkey_len);
	if (!c->user16 || !c->domain16 || !c->pass16 || !c->pubkey) { gn_cssp_clear(c); return GN_ENOMEM; }
	memcpy(c->pubkey, pubkey, pubkey_len);
	c->pubkey_len = pubkey_len;
	c->max_version = max_version < 2 ? 6 : (max_version > 6 ? 6 : max_version);
	return GN_OK;
}

void gn_cssp_clear(struct gn_cssp *c)
{
	gn_ntlm_clear(&c->ntlm);
	gn_free(c->user16, c->user16_len + 2);
	gn_free(c->domain16, c->domain16_len + 2);
	gn_free(c->pass16, c->pass16_len + 2);
	gn_free(c->pubkey, c->pubkey_len);
	gn_wipe(c, sizeof *c);
}

static int fail(struct gn_cssp *c, int e)
{
	c->error = e;
	c->state = 9;
	return e;
}

int gn_cssp_step(struct gn_cssp *c, const uint8_t *in, size_t inlen,
                 uint8_t *out, size_t size, size_t *outlen)
{
	struct gn_tsrequest q, r;
	uint8_t *tok = NULL, *pka = NULL;
	size_t toklen = 0, pkalen = 0, n;
	int e;

	memset(&q, 0, sizeof q);
	q.version = c->max_version;
	switch (c->state) {
	case 0: {
		uint8_t neg[64];
		size_t nl;
		/* 3.1.5 step 2: the first NTLM message, in negoTokens */
		if ((e = gn_ntlm_negotiate(&c->ntlm, neg, sizeof neg, &nl))) return fail(c, e);
		q.nego_token = neg; q.nego_token_len = nl;
		n = gn_tsrequest_encode(&q, out, size);
		if (n > size) return fail(c, GN_EBUFFER);
		*outlen = n;
		c->state = 1;
		return GN_CONTINUE;
	}
	case 1: {
		uint8_t hash[32];
		const uint8_t *bind = c->pubkey;
		size_t bindlen = c->pubkey_len;
		if ((e = gn_tsrequest_decode(in, inlen, &r))) return fail(c, e);
		if (r.has_error) { c->server_error = r.error_code; return fail(c, GN_ESERVER); }
		if (!r.nego_token) return fail(c, GN_EPROTOCOL);
		c->peer_version = r.version;
		c->version = r.version < c->max_version ? r.version : c->max_version;
		if (c->version < 2) c->version = 2;
		toklen = 1024 + r.nego_token_len + c->ntlm.user_len + c->ntlm.domain_len +
		         c->ntlm.ws_len + c->ntlm.spn_len;
		tok = gn_alloc(toklen);
		pkalen = 16 + (c->pubkey_len > 32 ? c->pubkey_len : 32);
		pka = gn_alloc(pkalen);
		if (!tok || !pka) { e = GN_ENOMEM; goto out; }
		if ((e = gn_ntlm_authenticate(&c->ntlm, r.nego_token, r.nego_token_len, tok, toklen, &n)))
			goto out;
		q.nego_token = tok; q.nego_token_len = n;
		/* step 3: bind to the TLS session's public key */
		if (c->version >= 5) {
			if (!(c->test & GN_CSSP_T_FIXED_NONCE) && gn_random(c->nonce, 32)) { e = GN_ENORANDOM; goto out; }
			gn_cssp_hash(c2s_magic, c->nonce, c->pubkey, c->pubkey_len, hash);
			bind = hash; bindlen = 32;
			q.client_nonce = c->nonce; q.client_nonce_len = 32;
		}
		if ((e = gn_ntlm_wrap(&c->ntlm, bind, bindlen, pka, pkalen, &n))) goto out;
		q.pub_key_auth = pka; q.pub_key_auth_len = n;
		n = gn_tsrequest_encode(&q, out, size);
		if (n > size) { e = GN_EBUFFER; goto out; }
		*outlen = n;
		c->state = 2;
		e = GN_CONTINUE;
		goto out;
	}
	case 2: {
		uint8_t want[32], *cred = NULL;
		size_t credlen, plen;
		unsigned diff = 0;
		size_t i;
		if ((e = gn_tsrequest_decode(in, inlen, &r))) return fail(c, e);
		if (r.has_error) { c->server_error = r.error_code; return fail(c, GN_ESERVER); }
		if (!r.pub_key_auth || r.pub_key_auth_len < 16) return fail(c, GN_EPROTOCOL);
		pkalen = r.pub_key_auth_len;
		pka = gn_alloc(pkalen);
		if (!pka) return fail(c, GN_ENOMEM);
		if ((e = gn_ntlm_unwrap(&c->ntlm, r.pub_key_auth, r.pub_key_auth_len, pka, pkalen, &plen)))
			goto out;
		/* step 5: check the server's answer */
		if (c->version >= 5) {
			gn_cssp_hash(s2c_magic, c->nonce, c->pubkey, c->pubkey_len, want);
			if (plen != 32) diff = 1;
			else for (i = 0; i < 32; i++) diff |= want[i] ^ pka[i];
		} else {
			if (plen != c->pubkey_len) diff = 1;
			else {
				diff |= (uint8_t)(c->pubkey[0] + 1) ^ pka[0];
				for (i = 1; i < plen; i++) diff |= c->pubkey[i] ^ pka[i];
			}
		}
		if (diff) { e = GN_EPUBKEY; goto out; }
		/* the credentials, sealed, in authInfo */
		credlen = gn_tscredentials_encode(c->domain16, c->domain16_len, c->user16, c->user16_len,
		                                  c->pass16, c->pass16_len, NULL, 0);
		cred = gn_alloc(credlen);
		toklen = credlen + 16;
		tok = gn_alloc(toklen);
		if (!cred || !tok) { gn_free(cred, credlen); e = GN_ENOMEM; goto out; }
		gn_tscredentials_encode(c->domain16, c->domain16_len, c->user16, c->user16_len,
		                        c->pass16, c->pass16_len, cred, credlen);
		e = gn_ntlm_wrap(&c->ntlm, cred, credlen, tok, toklen, &n);
		gn_free(cred, credlen);
		if (e) goto out;
		q.auth_info = tok; q.auth_info_len = n;
		n = gn_tsrequest_encode(&q, out, size);
		if (n > size) { e = GN_EBUFFER; goto out; }
		*outlen = n;
		c->state = 3;
		e = GN_OK;
		goto out;
	}
	default:
		return GN_ESTATE;
	}
out:
	gn_free(tok, toklen);
	gn_free(pka, pkalen);
	if (e < 0) fail(c, e);
	return e;
}
