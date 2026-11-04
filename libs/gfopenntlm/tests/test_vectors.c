/* gfopenntlm's PC tests: the published vectors of RFC 1320 (MD4),
 * RFC 1321 (MD5), RFC 2202 (HMAC-MD5), RFC 6229 (RC4), FIPS 180-2's
 * examples (SHA-256) and [MS-NLMP] 4.2 (NTLM), then CredSSP ([MS-CSSP]
 * has no vectors) end to end against a test server written from the
 * same specifications.  MIT, Copyright (c) 2026 AmiCradle Team. */
#include "../src/gn_internal.h"
#include <clib/gfopenntlm_protos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int pass, failed;

static size_t unhex(const char *s, uint8_t *out)
{
	size_t n = 0;
	int hi = -1;
	for (; *s; s++) {
		int v;
		if (*s >= '0' && *s <= '9') v = *s - '0';
		else if (*s >= 'a' && *s <= 'f') v = *s - 'a' + 10;
		else if (*s >= 'A' && *s <= 'F') v = *s - 'A' + 10;
		else continue;
		if (hi < 0) hi = v; else { out[n++] = (uint8_t)(hi << 4 | v); hi = -1; }
	}
	return n;
}

static void check_bytes(const char *what, const uint8_t *got, size_t len, const char *hex)
{
	uint8_t want[1024];
	size_t wl = unhex(hex, want), i;
	if (wl == len && !memcmp(got, want, len)) { pass++; printf("ok   %s\n", what); return; }
	failed++;
	printf("FAIL %s\n  got  ", what);
	for (i = 0; i < len; i++) printf("%02x", got[i]);
	printf("\n  want ");
	for (i = 0; i < wl; i++) printf("%02x", want[i]);
	printf("\n");
}

static void check(const char *what, int ok)
{
	if (ok) { pass++; printf("ok   %s\n", what); }
	else { failed++; printf("FAIL %s\n", what); }
}

static void test_md4(void)
{
	static const char *const v[][2] = {
		{ "", "31d6cfe0d16ae931b73c59d7e0c089c0" },
		{ "a", "bde52cb31de33e46245e05fbdbd6fb24" },
		{ "abc", "a448017aaf21d8525fc10ae87aa6729d" },
		{ "message digest", "d9130a8164549fe818874806e1c7014b" },
		{ "abcdefghijklmnopqrstuvwxyz", "d79e1c308aa5bbcdeea8ed63df412da9" },
		{ "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "043f8582f241db351ce627e153e7f0e4" },
		{ "12345678901234567890123456789012345678901234567890123456789012345678901234567890", "e33b4ddc9c38f2199c3e7b164fcc0536" },
	};
	size_t i;
	for (i = 0; i < sizeof v / sizeof v[0]; i++) {
		uint8_t d[16]; char name[64];
		gn_md4(v[i][0], strlen(v[i][0]), d);
		snprintf(name, sizeof name, "RFC 1320 MD4 #%u", (unsigned)i + 1);
		check_bytes(name, d, 16, v[i][1]);
	}
}

static void test_md5(void)
{
	static const char *const v[][2] = {
		{ "", "d41d8cd98f00b204e9800998ecf8427e" },
		{ "a", "0cc175b9c0f1b6a831c399e269772661" },
		{ "abc", "900150983cd24fb0d6963f7d28e17f72" },
		{ "message digest", "f96b697d7cb7938d525a2f31aaf161d0" },
		{ "abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b" },
		{ "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "d174ab98d277d9f5a5611c2c9f419d9f" },
		{ "12345678901234567890123456789012345678901234567890123456789012345678901234567890", "57edf4a22be3c955ac49da2e2107b67a" },
	};
	size_t i;
	for (i = 0; i < sizeof v / sizeof v[0]; i++) {
		uint8_t d[16]; char name[64];
		gn_md5(v[i][0], strlen(v[i][0]), d);
		snprintf(name, sizeof name, "RFC 1321 MD5 #%u", (unsigned)i + 1);
		check_bytes(name, d, 16, v[i][1]);
	}
}

static void test_hmac(void)
{
	uint8_t k[80], d[80], m[16];
	memset(k, 0x0b, 16); gn_hmac_md5(k, 16, "Hi There", 8, m);
	check_bytes("RFC 2202 HMAC-MD5 #1", m, 16, "9294727a3638bb1c13f48ef8158bfc9d");
	gn_hmac_md5("Jefe", 4, "what do ya want for nothing?", 28, m);
	check_bytes("RFC 2202 HMAC-MD5 #2", m, 16, "750c783e6ab0b503eaa86e310a5db738");
	memset(k, 0xaa, 16); memset(d, 0xdd, 50); gn_hmac_md5(k, 16, d, 50, m);
	check_bytes("RFC 2202 HMAC-MD5 #3", m, 16, "56be34521d144c88dbb8c733f0e8b3f6");
	{ int i; for (i = 0; i < 25; i++) k[i] = (uint8_t)(i + 1); }
	memset(d, 0xcd, 50); gn_hmac_md5(k, 25, d, 50, m);
	check_bytes("RFC 2202 HMAC-MD5 #4", m, 16, "697eaf0aca3a3aea3a75164746ffaa79");
	memset(k, 0x0c, 16); gn_hmac_md5(k, 16, "Test With Truncation", 20, m);
	check_bytes("RFC 2202 HMAC-MD5 #5", m, 16, "56461ef2342edc00f9bab995690efd4c");
	memset(k, 0xaa, 80);
	gn_hmac_md5(k, 80, "Test Using Larger Than Block-Size Key - Hash Key First", 54, m);
	check_bytes("RFC 2202 HMAC-MD5 #6", m, 16, "6b1ab7fe4bd7bf8f0b62e6ce61b9d0cd");
	gn_hmac_md5(k, 80, "Test Using Larger Than Block-Size Key and Larger Than One Block-Size Data", 73, m);
	check_bytes("RFC 2202 HMAC-MD5 #7", m, 16, "6f630fad67cda0ee1fb1f562db3aa53e");
}

static void test_rc4(void)
{
	uint8_t key[16], z[512], ks[512];
	struct gn_rc4 r;
	memset(z, 0, sizeof z);
	unhex("0102030405", key);
	gn_rc4_init(&r, key, 5); gn_rc4_crypt(&r, z, ks, 512);
	check_bytes("RFC 6229 RC4 40-bit offset 0", ks, 16, "b2396305f03dc027ccc3524a0a1118a8");
	check_bytes("RFC 6229 RC4 40-bit offset 16", ks + 16, 16, "6982944f18fc82d589c403a47a0d0919");
	check_bytes("RFC 6229 RC4 40-bit offset 240", ks + 240, 16, "28cb1132c96ce286421dcaadb8b69eae");
	check_bytes("RFC 6229 RC4 40-bit offset 496", ks + 496, 16, "42b7d0cdd918a8a33dd51781c81f4041");
	unhex("0102030405060708090a0b0c0d0e0f10", key);
	gn_rc4_init(&r, key, 16); gn_rc4_crypt(&r, z, ks, 272);
	check_bytes("RFC 6229 RC4 128-bit offset 0", ks, 16, "9ac7cc9a609d1ef7b2932899cde41b97");
	check_bytes("RFC 6229 RC4 128-bit offset 16", ks + 16, 16, "5248c4959014126a6e8a84f11d1a9e1c");
	check_bytes("RFC 6229 RC4 128-bit offset 256", ks + 256, 16, "d39d566bc6bce30107681515 49f3873f");
}

static void test_sha256(void)
{
	uint8_t d[32];
	static uint8_t a[1000000];
	gn_sha256("abc", 3, d);
	check_bytes("FIPS 180-2 SHA-256 \"abc\"", d, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	gn_sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, d);
	check_bytes("FIPS 180-2 SHA-256 448 bits", d, 32, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	memset(a, 'a', sizeof a);
	gn_sha256(a, sizeof a, d);
	check_bytes("FIPS 180-2 SHA-256 a million a", d, 32, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

/* [MS-NLMP] 4.2.4.3: the CHALLENGE_MESSAGE */
static const char chal_hex[] =
	"4e544c4d53535000020000000c000c003800000033828ae20123456789abcdef"
	"00000000000000002400240044000000060070170000000f5300650072007600"
	"6500720002000c0044006f006d00610069006e0001000c005300650072007600"
	"6500720000000000";
/* [MS-NLMP] 4.2.4.3: the AUTHENTICATE_MESSAGE */
static const char auth_hex[] =
	"4e544c4d5353500003000000180018006c00000054005400840000000c000c00"
	"480000000800080054000000100010005c00000010001000d800000035828 8e2"
	"0501280a0000000f44006f006d00610069006e00550073006500720043004f00"
	"4d0050005500540045005200 86c35097ac9cec102554764a57cccc19aaaaaaaa"
	"aaaaaaaa68cd0ab851e51c96aabc927bebef6a1c010100000000000000000000"
	"00000000aaaaaaaaaaaaaaaa0000000002000c0044006f006d00610069006e00"
	"01000c005300650072007600650072000000000000000000c5dad2544fc97990"
	"94ce1ce90bc9d03e";

static void test_nlmp(void)
{
	struct gn_ntlm n;
	uint8_t pw[32], buf[1024], chal[256], out[64], key[16];
	size_t pl, len, cl;
	uint8_t *p16 = gn_utf16("Password", 0, &pl);
	(void)pw;
	gn_md4(p16, pl, key);
	check_bytes("MS-NLMP 4.2.2.1.2 NTOWFv1(\"Password\")", key, 16, "a4f49c406510bdcab6824ee7c30fd852");
	gn_free(p16, pl + 2);

	check(".. init", gn_ntlm_init(&n, "User", "Domain", "Password", "COMPUTER", NULL) == 0);
	check_bytes("MS-NLMP 4.2.4.1.1 NTOWFv2", n.response_key, 16, "0c868a403bfd7a93a3001ef22ef02e3f");
	n.test = GN_NTLM_T_FIXED | GN_NTLM_T_RAW;
	n.t_time = 0;
	memset(n.t_client_challenge, 0xaa, 8);
	memset(n.t_session_key, 0x55, 16);
	unhex("0501280a0000000f", n.version);
	check(".. NEGOTIATE", gn_ntlm_negotiate(&n, buf, sizeof buf, &len) == 0 && len == 40);
	cl = unhex(chal_hex, chal);
	check(".. the 4.2.4.3 CHALLENGE is 0x68 bytes", cl == 0x68);
	check(".. AUTHENTICATE", gn_ntlm_authenticate(&n, chal, cl, buf, sizeof buf, &len) == 0);
	check_bytes("MS-NLMP 4.2.4.1.2 SessionBaseKey", n.session_base_key, 16, "8de40ccadbc14a82f15cb0ad0de95ca3");
	check_bytes("MS-NLMP 4.2.4.2.1 LMv2 response", buf + 0x6c, 24, "86c35097ac9cec102554764a57cccc19aaaaaaaaaaaaaaaa");
	check_bytes("MS-NLMP 4.2.4.2.2 NTProofStr", buf + 0x84, 16, "68cd0ab851e51c96aabc927bebef6a1c");
	check_bytes("MS-NLMP 4.2.4.1.3 temp", buf + 0x94, 0x44,
		"0101000000000000 0000000000000000 aaaaaaaaaaaaaaaa 00000000"
		"02000c0044006f006d00610069006e00 01000c00530065007200760065007200"
		"00000000 00000000");
	check_bytes("MS-NLMP 4.2.4.2.3 encrypted session key", buf + 0xd8, 16, "c5dad2544fc9799094ce1ce90bc9d03e");
	check_bytes("MS-NLMP 4.2.4.3 AUTHENTICATE_MESSAGE (0xe8 bytes)", buf, len, auth_hex);
	check_bytes("MS-NLMP 4.2.4.4 sealing key", n.client_seal_key, 16, "59f600973cc4960a25480a7c196e4c58");
	check_bytes("MS-NLMP 4.2.4.4 signing key", n.client_sign, 16, "4788dc861b4782f35d43fd98fe1a2d39");
	{
		static const uint8_t plain[18] = { 'P', 0, 'l', 0, 'a', 0, 'i', 0, 'n', 0, 't', 0, 'e', 0, 'x', 0, 't', 0 };
		check(".. wrap", gn_ntlm_wrap(&n, plain, 18, out, sizeof out, &len) == 0 && len == 34);
		check_bytes("MS-NLMP 4.2.4.4 GSS_WrapEx data", out + 16, 18, "54e50165bf1936dc996020c1811b0f06fb5f");
		check_bytes("MS-NLMP 4.2.4.4 GSS_WrapEx signature", out, 16, "010000007fb38ec5c55d497600000000");
	}
	/* the receiving side: a test server with the same key */
	{
		struct gn_ntlm s;
		uint8_t w[64], u[64];
		size_t wl, ul;
		int k, ok = 1;
		memset(&s, 0, sizeof s);
		gn_ntlm_set_keys(&s, n.exported_key, n.flags, 1);
		for (k = 0; k < 3; k++) {
			char msg[32];
			snprintf(msg, sizeof msg, "server message %d", k);
			ok &= gn_ntlm_wrap(&s, (const uint8_t *)msg, strlen(msg), w, sizeof w, &wl) == 0;
			ok &= gn_ntlm_unwrap(&n, w, wl, u, sizeof u, &ul) == 0 && ul == strlen(msg) && !memcmp(u, msg, ul);
		}
		check("GSS_UnwrapEx round trip, sequence 0..2", ok);
		gn_ntlm_wrap(&s, (const uint8_t *)"tampered", 8, w, sizeof w, &wl);
		w[20] ^= 1;
		check("GSS_UnwrapEx rejects a changed message", gn_ntlm_unwrap(&n, w, wl, u, sizeof u, &ul) == GN_EINTEGRITY);
		gn_ntlm_clear(&s);
	}
	gn_ntlm_clear(&n);
}

/* ---- a CredSSP test server ([MS-CSSP] 3.1.5 from the server's side) ---- */
struct srv {
	const char *password;
	uint32_t version;
	int bad_pubkey;          /* answer with the wrong binding */
	uint8_t pubkey[128]; size_t pklen;
	uint8_t neg[64]; size_t neg_len;
	uint8_t chal[256]; size_t chal_len;
	struct gn_ntlm keys;
	uint8_t nonce[32];
	char got_user[64], got_pass[64];
	int verified_mic;
};

static void ascii16(const uint8_t *p, size_t len, char *out, size_t size)
{
	size_t i;
	for (i = 0; i + 1 < len && i / 2 + 1 < size; i += 2) out[i / 2] = (char)p[i];
	out[i / 2] = 0;
}

static size_t srv_challenge(struct srv *s, uint8_t *out, size_t size)
{
	uint8_t m[256], *q;
	struct gn_tsrequest r;
	size_t ti;
	static const uint8_t dom[] = { 'F', 0, 'A', 0, 'K', 0, 'E', 0 }, cmp[] = { 'S', 0, 'R', 0, 'V', 0 };
	memset(m, 0, sizeof m);
	memcpy(m, "NTLMSSP", 8);
	gn_put32(m + 8, 2);
	gn_put16(m + 12, 8); gn_put16(m + 14, 8); gn_put32(m + 16, 56);
	gn_put32(m + 20, 0xe28a8235u);
	memcpy(m + 24, "\x11\x22\x33\x44\x55\x66\x77\x88", 8);
	memcpy(m + 48, "\x0a\x00\x61\x4a\x00\x00\x00\x0f", 8);
	memcpy(m + 56, dom, 8);
	q = m + 64; ti = 64;
	gn_put16(q, 2); gn_put16(q + 2, 8); memcpy(q + 4, dom, 8); q += 12;
	gn_put16(q, 1); gn_put16(q + 2, 6); memcpy(q + 4, cmp, 6); q += 10;
	gn_put16(q, 7); gn_put16(q + 2, 8); gn_put32(q + 4, 0x12345678); gn_put32(q + 8, 0x01dc0000); q += 12;
	gn_put16(q, 0); gn_put16(q + 2, 0); q += 4;
	gn_put16(m + 40, (uint16_t)(q - m - ti)); gn_put16(m + 42, (uint16_t)(q - m - ti)); gn_put32(m + 44, (uint32_t)ti);
	s->chal_len = (size_t)(q - m);
	memcpy(s->chal, m, s->chal_len);
	memset(&r, 0, sizeof r);
	r.version = s->version;
	r.nego_token = s->chal; r.nego_token_len = s->chal_len;
	return gn_tsrequest_encode(&r, out, size);
}

/* checks AUTHENTICATE, MIC and pubKeyAuth; answers pubKeyAuth or an error */
static size_t srv_authenticate(struct srv *s, const uint8_t *in, size_t len, uint8_t *out, size_t size)
{
	struct gn_tsrequest r, a;
	const uint8_t *m, *nt, *user, *dom;
	size_t ml, ntl, ul, dl;
	uint8_t uu[128], key[16], nth[16], proof[16], sbk[16], exported[16], buf[512], pk[128];
	size_t i, pl, uul;
	struct gn_hmac_md5 h;
	uint8_t *pw16;
	size_t pwl;
	memset(&a, 0, sizeof a);
	a.version = s->version;
	if (gn_tsrequest_decode(in, len, &r) || !r.nego_token || !r.pub_key_auth) return 0;
	m = r.nego_token; ml = r.nego_token_len;
	if (ml < 88 || gn_get32(m + 8) != 3) return 0;
	nt = m + gn_get32(m + 24); ntl = gn_get16(m + 20);
	dom = m + gn_get32(m + 32); dl = gn_get16(m + 28);
	user = m + gn_get32(m + 40); ul = gn_get16(m + 36);
	ascii16(user, ul, s->got_user, sizeof s->got_user);
	for (i = 0; i < ul && i < sizeof uu; i += 2) {
		uu[i] = (user[i] >= 'a' && user[i] <= 'z') ? (uint8_t)(user[i] - 32) : user[i];
		uu[i + 1] = user[i + 1];
	}
	uul = ul;
	pw16 = gn_utf16(s->password, 0, &pwl);
	gn_md4(pw16, pwl, nth);
	gn_free(pw16, pwl + 2);
	gn_ntowfv2(nth, uu, uul, dom, dl, key);
	gn_hmac_md5_init(&h, key, 16);
	gn_hmac_md5_update(&h, s->chal + 24, 8);
	gn_hmac_md5_update(&h, nt + 16, ntl - 16);
	gn_hmac_md5_final(&h, proof);
	if (memcmp(proof, nt, 16)) {
		a.has_error = 1; a.error_code = 0xc000006du;   /* STATUS_LOGON_FAILURE */
		return gn_tsrequest_encode(&a, out, size);
	}
	gn_hmac_md5(key, 16, proof, 16, sbk);
	{
		struct gn_rc4 rc;
		gn_rc4_init(&rc, sbk, 16);
		gn_rc4_crypt(&rc, m + gn_get32(m + 56), exported, 16);
	}
	/* the MIC */
	memcpy(buf, m, ml);
	memset(buf + 72, 0, 16);
	gn_hmac_md5_init(&h, exported, 16);
	gn_hmac_md5_update(&h, s->neg, s->neg_len);
	gn_hmac_md5_update(&h, s->chal, s->chal_len);
	gn_hmac_md5_update(&h, buf, ml);
	gn_hmac_md5_final(&h, proof);
	s->verified_mic = !memcmp(proof, m + 72, 16);
	memset(&s->keys, 0, sizeof s->keys);
	gn_ntlm_set_keys(&s->keys, exported, gn_get32(m + 60), 1);
	if (gn_ntlm_unwrap(&s->keys, r.pub_key_auth, r.pub_key_auth_len, buf, sizeof buf, &pl)) return 0;
	if (s->version >= 5) {
		uint8_t want[32];
		if (!r.client_nonce || r.client_nonce_len != 32) return 0;
		memcpy(s->nonce, r.client_nonce, 32);
		gn_cssp_hash("CredSSP Client-To-Server Binding Hash", s->nonce, s->pubkey, s->pklen, want);
		if (pl != 32 || memcmp(buf, want, 32)) return 0;
		gn_cssp_hash("CredSSP Server-To-Client Binding Hash", s->nonce, s->pubkey, s->pklen, pk);
		pl = 32;
	} else {
		if (pl != s->pklen || memcmp(buf, s->pubkey, pl)) return 0;
		memcpy(pk, s->pubkey, pl);
		pk[0]++;
	}
	if (s->bad_pubkey) pk[1] ^= 0x40;
	gn_ntlm_wrap(&s->keys, pk, pl, buf, sizeof buf, &pl);
	a.pub_key_auth = buf; a.pub_key_auth_len = pl;
	return gn_tsrequest_encode(&a, out, size);
}

static int srv_credentials(struct srv *s, const uint8_t *in, size_t len)
{
	struct gn_tsrequest r;
	uint8_t buf[512];
	size_t pl;
	struct gn_der d, seq, f, o, pc, x;
	if (gn_tsrequest_decode(in, len, &r) || !r.auth_info) return 0;
	if (gn_ntlm_unwrap(&s->keys, r.auth_info, r.auth_info_len, buf, sizeof buf, &pl)) return 0;
	d.p = buf; d.len = pl;
	if (gn_der_get(&d, 0x30, &seq) || gn_der_get(&seq, 0xa0, &f) || gn_der_get(&seq, 0xa1, &f) ||
	    gn_der_get(&f, 0x04, &o) || gn_der_get(&o, 0x30, &pc) || gn_der_get(&pc, 0xa0, &f) ||
	    gn_der_get(&pc, 0xa1, &f) || gn_der_get(&f, 0x04, &x)) return 0;
	if (gn_der_get(&pc, 0xa2, &f) || gn_der_get(&f, 0x04, &x)) return 0;
	ascii16(x.p, x.len, s->got_pass, sizeof s->got_pass);
	return 1;
}

static void credssp_run(uint32_t version, const char *client_pw, int bad_pubkey, int want)
{
	struct GNCredSSPConfig cfg;
	struct GNCredSSP *c;
	struct srv s;
	uint8_t a[8192], b[8192];
	ULONG al;
	size_t bl;
	LONG e, r;
	char name[128];
	memset(&s, 0, sizeof s);
	s.password = "Password";
	s.version = version;
	s.bad_pubkey = bad_pubkey;
	s.pklen = unhex("3048024100c0ffee0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f2021222324252627"
	                "28292a2b2c2d2e2f303132333435363738393a3b3c3d3e0203010001", s.pubkey);
	memset(&cfg, 0, sizeof cfg);
	cfg.Size = sizeof cfg;
	cfg.User = "User"; cfg.Domain = "FAKE"; cfg.Password = client_pw; cfg.Workstation = "AMIGA";
	cfg.ServerName = "srv.example"; cfg.PublicKey = s.pubkey; cfg.PublicKeyLength = (ULONG)s.pklen;
	c = GN_CredSSPNew(&cfg, &e);
	snprintf(name, sizeof name, "CredSSP v%u%s%s", (unsigned)version,
	         strcmp(client_pw, "Password") ? " wrong password" : "", bad_pubkey ? " bad server binding" : "");
	if (!c) { check(name, 0); return; }
	r = GN_CredSSPStep(c, NULL, 0, a, sizeof a, &al);
	{
		struct gn_tsrequest q;
		if (r != GNERR_CONTINUE || gn_tsrequest_decode(a, al, &q) || !q.nego_token) { check(name, 0); goto out; }
		memcpy(s.neg, q.nego_token, q.nego_token_len); s.neg_len = q.nego_token_len;
	}
	bl = srv_challenge(&s, b, sizeof b);
	r = GN_CredSSPStep(c, b, (ULONG)bl, a, sizeof a, &al);
	if (r != GNERR_CONTINUE) { printf("  (step 2: %ld)\n", (long)r); check(name, 0); goto out; }
	bl = srv_authenticate(&s, a, al, b, sizeof b);
	if (!bl) { printf("  (the test server refused the AUTHENTICATE)\n"); check(name, 0); goto out; }
	r = GN_CredSSPStep(c, b, (ULONG)bl, a, sizeof a, &al);
	if (want == GNERR_SERVER) {
		snprintf(name + strlen(name), sizeof name - strlen(name), ": errorCode 0x%08lx", (unsigned long)GN_CredSSPGetAttr(c, GNA_SERVERERROR));
		check(name, r == GNERR_SERVER && GN_CredSSPGetAttr(c, GNA_SERVERERROR) == 0xc000006du);
	} else if (want == GNERR_PUBKEY) {
		check(name, r == GNERR_PUBKEY);
	} else {
		int ok = r == GNERR_OK && srv_credentials(&s, a, al) && s.verified_mic &&
		         !strcmp(s.got_user, "User") && !strcmp(s.got_pass, "Password") &&
		         GN_CredSSPGetAttr(c, GNA_VERSION) == version;
		check(name, ok);
	}
out:
	GN_CredSSPFree(c);
	gn_ntlm_clear(&s.keys);
}

static void test_der(void)
{
	struct gn_tsrequest q, r;
	uint8_t b[64], tok[3] = { 1, 2, 3 };
	size_t n;
	memset(&q, 0, sizeof q);
	q.version = 6; q.nego_token = tok; q.nego_token_len = 3;
	n = gn_tsrequest_encode(&q, b, sizeof b);
	/* SEQUENCE { [0] INTEGER 6, [1] SEQUENCE OF SEQUENCE { [0] OCTET STRING 010203 } } */
	check_bytes("X.690 TSRequest encoding", b, n, "3012 a003020106 a10b 3009 3007 a005 0403010203");
	check("X.690 TSRequest decoding", gn_tsrequest_decode(b, n, &r) == 0 && r.version == 6 &&
	      r.nego_token_len == 3 && !memcmp(r.nego_token, tok, 3));
	n = unhex("300d a003020106 a406 0204c000006d", b);
	check("X.690 errorCode as a negative INTEGER", gn_tsrequest_decode(b, n, &r) == 0 && r.has_error &&
	      r.error_code == 0xc000006du);
	check("X.690 message length", gn_der_message_length(b, 2) == 15);
	n = gn_tscredentials_encode((const uint8_t *)"D\0", 2, (const uint8_t *)"U\0", 2, (const uint8_t *)"P\0", 2, b, sizeof b);
	check_bytes("MS-CSSP TSCredentials/TSPasswordCreds encoding", b, n,
	            "301d a003020101 a1160414 3012 a0040402 4400 a1040402 5500 a2040402 5000");
}

static void test_api(void)
{
	uint8_t d[64], k[5] = { 1, 2, 3, 4, 5 }, z[16];
	struct GNNTLMConfig nc;
	struct GNNTLM *n;
	LONG e;
	ULONG l;
	check("GN_Version 1.0", GN_Version() == 0x10000);
	check("GN_Digest MD4", GN_Digest(GNALG_MD4, "abc", 3, d) == 0 && !memcmp(d, "\xa4\x48\x01\x7a", 4));
	check("GN_HMAC MD5", GN_HMAC(GNALG_MD5, "Jefe", 4, "what do ya want for nothing?", 28, d) == 0 &&
	      !memcmp(d, "\x75\x0c\x78\x3e", 4));
	memset(z, 0, 16);
	check("GN_RC4", GN_RC4(k, 5, z, 16) == 0 && !memcmp(z, "\xb2\x39\x63\x05", 4));
	memset(&nc, 0, sizeof nc);
	nc.Size = 8;
	check("GN_NTLMNew refuses an old Size", GN_NTLMNew(&nc, &e) == NULL && e == GNERR_BADARGS);
	nc.Size = sizeof nc; nc.User = "u"; nc.Password = "p";
	n = GN_NTLMNew(&nc, &e);
	check("GN_NTLMNew", n != NULL && e == GNERR_OK);
	check("GN_NTLMNegotiate", GN_NTLMNegotiate(n, d, 40, &l) == 0 && l == 40 && !memcmp(d, "NTLMSSP", 8));
	check("GN_NTLMNegotiate twice is out of order", GN_NTLMNegotiate(n, d, 40, &l) == GNERR_STATE);
	GN_NTLMFree(n);
}

int main(void)
{
	test_md4();
	test_md5();
	test_hmac();
	test_rc4();
	test_sha256();
	test_nlmp();
	test_der();
	test_api();
	credssp_run(6, "Password", 0, GNERR_OK);
	credssp_run(5, "Password", 0, GNERR_OK);
	credssp_run(4, "Password", 0, GNERR_OK);
	credssp_run(3, "Password", 0, GNERR_OK);
	credssp_run(2, "Password", 0, GNERR_OK);
	credssp_run(6, "not-the-password", 0, GNERR_SERVER);
	credssp_run(6, "Password", 1, GNERR_PUBKEY);
	credssp_run(3, "Password", 1, GNERR_PUBKEY);
	printf("%d passed, %d failed\n", pass, failed);
	return failed ? 1 : 0;
}
