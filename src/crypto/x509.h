/*
 * GFAmigaRDP: a minimal X.509 v3 (DER) reader for the TLS and CredSSP
 * layers: names, the public key, subjectAltName, the host check and the
 * tls-server-end-point channel binding. It reads; it does not validate a
 * chain (that is gfopentls.library's job). Written from RFC 5280, X.690,
 * RFC 6125 and RFC 5929.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_X509_H
#define RDP_X509_H

#include <stddef.h>
#include <stdint.h>

struct rdp_x509
{
	const uint8_t* der;
	size_t der_len;
	const uint8_t* tbs; size_t tbs_len;
	const uint8_t* serial; size_t serial_len;
	const uint8_t* issuer; size_t issuer_len;     /* Name, DER */
	const uint8_t* subject; size_t subject_len;   /* Name, DER */
	const uint8_t* spki; size_t spki_len;         /* SubjectPublicKeyInfo, DER, whole */
	const uint8_t* pubkey; size_t pubkey_len;     /* the BIT STRING contents (no unused-bits byte) */
	const uint8_t* rsa_n; size_t rsa_n_len;       /* rsaEncryption keys only */
	const uint8_t* rsa_e; size_t rsa_e_len;
	const uint8_t* san; size_t san_len;           /* subjectAltName extension value, or NULL */
	char not_before[16], not_after[16];           /* as in the certificate (UTCTime/GeneralizedTime) */
	int sig_hash;                                 /* AMIPR_SHA* (amipr.h) of the signature algorithm, 0 unknown */
	int key_rsa;                                  /* 1 for rsaEncryption */
	uint32_t key_bits;
};
int rdp_x509_parse(struct rdp_x509* c, const uint8_t* der, size_t len);
/* Name to "CN = x, O = y" (RFC 4514-like, outermost first) */
int rdp_x509_name_string(const uint8_t* name, size_t len, char* buf, size_t size);
int rdp_x509_common_name(const struct rdp_x509* c, char* buf, size_t size);
typedef int (*rdp_san_cb)(void* user, int type, const uint8_t* value, size_t len); /* 2 DNS, 7 IP */
int rdp_x509_foreach_san(const struct rdp_x509* c, rdp_san_cb cb, void* user);
int rdp_x509_match_host(const struct rdp_x509* c, const char* host); /* 1 match, 0 no */
/* tls-server-end-point (RFC 5929): the hash the signature uses, SHA-256 for MD5/SHA-1 */
int rdp_x509_channel_binding(const struct rdp_x509* c, uint8_t* out, size_t size, int* alg);

#endif
