/* gfopenntlm: the portable core's internal interface.
 * Written from the published specifications only: RFC 1320 (MD4),
 * RFC 1321 (MD5), RFC 2104 (HMAC), FIPS 180-4 (SHA-256), the public RC4
 * description, ITU-T X.690 (DER), [MS-NLMP] (NTLM) and [MS-CSSP] (CredSSP).
 * MIT licensed. Copyright (c) 2026 AmiCradle Team. */
#ifndef GN_INTERNAL_H
#define GN_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

/* results, the same values as GNERR_* in libraries/gfopenntlm.h */
#define GN_OK           0
#define GN_CONTINUE     1
#define GN_ENOMEM      -1
#define GN_EBADARGS    -2
#define GN_EBUFFER     -3
#define GN_EPROTOCOL   -4
#define GN_ESERVER     -5
#define GN_EPUBKEY     -6
#define GN_EINTEGRITY  -7
#define GN_ENORANDOM   -8
#define GN_ESTATE      -9
#define GN_EUNSUPPORTED -10

/* ---- digests (gn_md4.c, gn_md5.c, gn_sha256.c) ---- */
struct gn_md4 { uint32_t h[4]; uint8_t buf[64]; uint64_t len; };
struct gn_md5 { uint32_t h[4]; uint8_t buf[64]; uint64_t len; };
struct gn_sha256 { uint32_t h[8]; uint8_t buf[64]; uint64_t len; };

void gn_md4_init(struct gn_md4 *c);
void gn_md4_update(struct gn_md4 *c, const void *data, size_t len);
void gn_md4_final(struct gn_md4 *c, uint8_t out[16]);
void gn_md4(const void *data, size_t len, uint8_t out[16]);

void gn_md5_init(struct gn_md5 *c);
void gn_md5_update(struct gn_md5 *c, const void *data, size_t len);
void gn_md5_final(struct gn_md5 *c, uint8_t out[16]);
void gn_md5(const void *data, size_t len, uint8_t out[16]);

void gn_sha256_init(struct gn_sha256 *c);
void gn_sha256_update(struct gn_sha256 *c, const void *data, size_t len);
void gn_sha256_final(struct gn_sha256 *c, uint8_t out[32]);
void gn_sha256(const void *data, size_t len, uint8_t out[32]);

/* HMAC-MD5 (RFC 2104) over up to three pieces (any may be NULL/0). */
struct gn_hmac_md5 { struct gn_md5 inner, outer; };
void gn_hmac_md5_init(struct gn_hmac_md5 *h, const void *key, size_t klen);
void gn_hmac_md5_update(struct gn_hmac_md5 *h, const void *data, size_t len);
void gn_hmac_md5_final(struct gn_hmac_md5 *h, uint8_t out[16]);
void gn_hmac_md5(const void *key, size_t klen, const void *data, size_t len, uint8_t out[16]);
void gn_hmac_sha256(const void *key, size_t klen, const void *data, size_t len, uint8_t out[32]);

/* ---- RC4 (gn_rc4.c) ---- */
struct gn_rc4 { uint8_t s[256]; uint8_t i, j; };
void gn_rc4_init(struct gn_rc4 *r, const void *key, size_t klen);
void gn_rc4_crypt(struct gn_rc4 *r, const void *in, void *out, size_t len);

/* ---- system (gn_sys_amiga.c / gn_sys_host.c) ---- */
void *gn_alloc(size_t n);          /* zeroed */
void gn_free(void *p, size_t n);   /* wipes n bytes first when n > 0 */
int gn_random(void *buf, size_t len); /* 0 on success */
uint64_t gn_filetime_now(void);    /* 100 ns ticks since 1601-01-01 UTC */
void gn_sys_cleanup(void);
void gn_wipe(void *p, size_t n);

/* ---- strings (gn_util.c) ---- */
/* UTF-8 -> UTF-16LE; upper=1 maps letters to upper case (ASCII and
 * Latin-1). Returns a gn_alloc'd buffer (+2 zero bytes) and its byte
 * length in *outlen, or NULL. */
uint8_t *gn_utf16(const char *s, int upper, size_t *outlen);
void gn_put16(uint8_t *p, uint16_t v);
void gn_put32(uint8_t *p, uint32_t v);
uint16_t gn_get16(const uint8_t *p);
uint32_t gn_get32(const uint8_t *p);

/* ---- NTLM (gn_ntlm.c), [MS-NLMP] ---- */
#define NTLMSSP_NEGOTIATE_UNICODE                  0x00000001u
#define NTLMSSP_NEGOTIATE_OEM                      0x00000002u
#define NTLMSSP_REQUEST_TARGET                     0x00000004u
#define NTLMSSP_NEGOTIATE_SIGN                     0x00000010u
#define NTLMSSP_NEGOTIATE_SEAL                     0x00000020u
#define NTLMSSP_NEGOTIATE_LM_KEY                   0x00000080u
#define NTLMSSP_NEGOTIATE_NTLM                     0x00000200u
#define NTLMSSP_NEGOTIATE_ALWAYS_SIGN              0x00008000u
#define NTLMSSP_NEGOTIATE_EXTENDED_SESSIONSECURITY 0x00080000u
#define NTLMSSP_NEGOTIATE_TARGET_INFO              0x00800000u
#define NTLMSSP_NEGOTIATE_VERSION                  0x02000000u
#define NTLMSSP_NEGOTIATE_128                      0x20000000u
#define NTLMSSP_NEGOTIATE_KEY_EXCH                 0x40000000u
#define NTLMSSP_NEGOTIATE_56                       0x80000000u

#define GN_NTLM_CLIENT_FLAGS (NTLMSSP_NEGOTIATE_UNICODE | NTLMSSP_REQUEST_TARGET | \
	NTLMSSP_NEGOTIATE_SIGN | NTLMSSP_NEGOTIATE_SEAL | NTLMSSP_NEGOTIATE_NTLM | \
	NTLMSSP_NEGOTIATE_ALWAYS_SIGN | NTLMSSP_NEGOTIATE_EXTENDED_SESSIONSECURITY | \
	NTLMSSP_NEGOTIATE_TARGET_INFO | NTLMSSP_NEGOTIATE_VERSION | NTLMSSP_NEGOTIATE_128 | \
	NTLMSSP_NEGOTIATE_KEY_EXCH | NTLMSSP_NEGOTIATE_56)

/* AV pair ids, [MS-NLMP] 2.2.2.1 */
#define MsvAvEOL             0
#define MsvAvNbComputerName  1
#define MsvAvNbDomainName    2
#define MsvAvFlags           6
#define MsvAvTimestamp       7
#define MsvAvTargetName      9
#define MsvChannelBindings  10

/* test controls: reproduce [MS-NLMP] 4.2.4 byte for byte */
#define GN_NTLM_T_FIXED   1  /* use the fixed time/client challenge/session key */
#define GN_NTLM_T_RAW     2  /* send the server's target info unchanged, no MIC */

struct gn_ntlm {
	uint8_t *user;   size_t user_len;    /* UTF-16LE */
	uint8_t *domain; size_t domain_len;
	uint8_t *ws;     size_t ws_len;
	uint8_t *spn;    size_t spn_len;
	uint8_t nt_hash[16];       /* MD4(UNICODE(password)) */
	uint8_t response_key[16];  /* NTOWFv2 */
	uint8_t *negotiate; size_t negotiate_len; /* kept for the MIC */
	uint32_t client_flags, flags;
	uint8_t version[8];
	int state;                 /* 0 new, 1 negotiate sent, 2 authenticated */
	/* test controls */
	unsigned test;
	uint64_t t_time;
	uint8_t t_client_challenge[8];
	uint8_t t_session_key[16];
	/* results */
	uint8_t session_base_key[16];
	uint8_t exported_key[16];
	uint8_t client_sign[16], server_sign[16];
	uint8_t client_seal_key[16], server_seal_key[16];
	struct gn_rc4 client_seal, server_seal;
	uint32_t send_seq, recv_seq;
	uint32_t peer_version;     /* the CHALLENGE's version field, low 32 bits */
};

/* error codes match libraries/gfopenntlm.h */
int gn_ntlm_init(struct gn_ntlm *n, const char *user, const char *domain,
                 const char *password, const char *workstation, const char *spn);
void gn_ntlm_clear(struct gn_ntlm *n);
int gn_ntlm_negotiate(struct gn_ntlm *n, uint8_t *out, size_t size, size_t *outlen);
int gn_ntlm_authenticate(struct gn_ntlm *n, const uint8_t *chal, size_t chal_len,
                         uint8_t *out, size_t size, size_t *outlen);
int gn_ntlm_wrap(struct gn_ntlm *n, const uint8_t *in, size_t len,
                 uint8_t *out, size_t size, size_t *outlen);
int gn_ntlm_unwrap(struct gn_ntlm *n, const uint8_t *in, size_t len,
                   uint8_t *out, size_t size, size_t *outlen);
/* key derivation from an exported session key ([MS-NLMP] 3.4.5.2, 3.4.5.3);
 * server=1 swaps the directions (for tests that play the server) */
void gn_ntlm_set_keys(struct gn_ntlm *n, const uint8_t exported[16], uint32_t flags, int server);
/* NTOWFv2 ([MS-NLMP] 3.3.2) from an NT hash and UTF-16 user/domain */
void gn_ntowfv2(const uint8_t nt_hash[16], const uint8_t *user16, size_t ulen,
                const uint8_t *dom16, size_t dlen, uint8_t out[16]);

/* ---- DER (gn_der.c), ITU-T X.690 ---- */
struct gn_der { const uint8_t *p; size_t len; };
/* read one TLV from d: tag, contents into v; advances d */
int gn_der_get(struct gn_der *d, uint8_t tag, struct gn_der *v);
int gn_der_peek(const struct gn_der *d);
size_t gn_der_hdr_size(size_t len);
size_t gn_der_hdr(uint8_t *p, uint8_t tag, size_t len);  /* p may be NULL */
/* total length of a DER message from its first bytes, 0 if more needed, -1 bad */
long gn_der_message_length(const uint8_t *p, size_t have);

/* ---- CredSSP (gn_cssp.c), [MS-CSSP] ---- */
struct gn_tsrequest {
	uint32_t version;
	const uint8_t *nego_token; size_t nego_token_len;
	const uint8_t *auth_info;  size_t auth_info_len;
	const uint8_t *pub_key_auth; size_t pub_key_auth_len;
	int has_error; uint32_t error_code;
	const uint8_t *client_nonce; size_t client_nonce_len;
};
size_t gn_tsrequest_encode(const struct gn_tsrequest *r, uint8_t *out, size_t size);
int gn_tsrequest_decode(const uint8_t *in, size_t len, struct gn_tsrequest *r);
size_t gn_tscredentials_encode(const uint8_t *dom, size_t dl, const uint8_t *user, size_t ul,
                               const uint8_t *pass, size_t pl, uint8_t *out, size_t size);

struct gn_cssp {
	struct gn_ntlm ntlm;
	uint8_t *user16, *domain16, *pass16; size_t user16_len, domain16_len, pass16_len;
	uint8_t *pubkey; size_t pubkey_len;
	uint32_t max_version, version, peer_version;
	uint8_t nonce[32];
	int state;
	int error;
	uint32_t server_error;
	unsigned test;  /* GN_CSSP_T_FIXED_NONCE */
};
#define GN_CSSP_T_FIXED_NONCE 1

int gn_cssp_init(struct gn_cssp *c, const char *user, const char *domain, const char *password,
                 const char *workstation, const char *server, const uint8_t *pubkey,
                 size_t pubkey_len, uint32_t max_version);
void gn_cssp_clear(struct gn_cssp *c);
int gn_cssp_step(struct gn_cssp *c, const uint8_t *in, size_t inlen,
                 uint8_t *out, size_t size, size_t *outlen);
void gn_cssp_hash(const char *magic, const uint8_t nonce[32], const uint8_t *pubkey,
                  size_t pklen, uint8_t out[32]);

#endif
