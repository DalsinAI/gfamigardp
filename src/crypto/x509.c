/*
 * GFAmigaRDP: a minimal X.509 v3 (DER) reader for TLS clients.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 *
 * Written from RFC 5280 (certificate structure), X.690 (DER), RFC 6125
 * (host names) and RFC 5929 (tls-server-end-point). It reads; it does not
 * validate chains - that is gfopentls.library's job. Pure C: no Amiga API.
 */
#include "x509.h"
#include "amipr.h" /* hashes for the channel binding */

#include <stdio.h>
#include <string.h>

struct der
{
	const uint8_t* p;
	size_t n;
};

/* read one TLV; *val gets the contents, *whole the full TLV */
static int der_next(struct der* d, int* tag, struct der* val, struct der* whole)
{
	size_t len, hdr = 2;
	if (d->n < 2)
		return -1;
	*tag = d->p[0];
	if ((d->p[0] & 0x1f) == 0x1f)
		return -1; /* multi-byte tags do not occur in certificates */
	len = d->p[1];
	if (len & 0x80)
	{
		size_t k = len & 0x7f, i;
		if (k == 0 || k > 4 || d->n < 2 + k)
			return -1;
		len = 0;
		for (i = 0; i < k; i++)
			len = (len << 8) | d->p[2 + i];
		hdr += k;
	}
	if (d->n - hdr < len)
		return -1;
	if (val)
	{
		val->p = d->p + hdr;
		val->n = len;
	}
	if (whole)
	{
		whole->p = d->p;
		whole->n = hdr + len;
	}
	d->p += hdr + len;
	d->n -= hdr + len;
	return 0;
}

static int der_expect(struct der* d, int want, struct der* val, struct der* whole)
{
	struct der save = *d;
	int tag;
	if (der_next(d, &tag, val, whole) != 0 || tag != want)
	{
		*d = save;
		return -1;
	}
	return 0;
}

static int oid_is(const struct der* oid, const uint8_t* bytes, size_t n)
{
	return oid->n == n && memcmp(oid->p, bytes, n) == 0;
}

static const uint8_t OID_RSA_ENC[] = { 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01 };
static const uint8_t OID_PKCS1_PREFIX[] = { 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01 };
static const uint8_t OID_ECDSA_PREFIX[] = { 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03 };
static const uint8_t OID_SAN[] = { 0x55, 0x1d, 0x11 };
static const uint8_t OID_CN[] = { 0x55, 0x04, 0x03 };

static int sig_hash_of(const struct der* oid)
{
	if (oid->n == sizeof(OID_PKCS1_PREFIX) + 1 &&
	    memcmp(oid->p, OID_PKCS1_PREFIX, sizeof(OID_PKCS1_PREFIX)) == 0)
	{
		switch (oid->p[sizeof(OID_PKCS1_PREFIX)])
		{
			case 4:  /* md5WithRSAEncryption */
			case 5:  /* sha1WithRSAEncryption */
				return AMIPR_SHA1;
			case 10: /* RSASSA-PSS: the hash is in the parameters; SHA-256 is
			          * what RDP servers use, and what we assume */
			case 11: return AMIPR_SHA256;
			case 12: return AMIPR_SHA384;
			case 13: return AMIPR_SHA512;
			default: return 0;
		}
	}
	if (oid->n == sizeof(OID_ECDSA_PREFIX) + 1 &&
	    memcmp(oid->p, OID_ECDSA_PREFIX, sizeof(OID_ECDSA_PREFIX)) == 0)
	{
		switch (oid->p[sizeof(OID_ECDSA_PREFIX)])
		{
			case 2: return AMIPR_SHA256;
			case 3: return AMIPR_SHA384;
			case 4: return AMIPR_SHA512;
			default: return 0;
		}
	}
	return 0;
}

static void copy_time(char* out, const struct der* t)
{
	size_t n = t->n < 15 ? t->n : 15;
	memcpy(out, t->p, n);
	out[n] = '\0';
}

static size_t int_bits(const uint8_t* p, size_t n)
{
	uint8_t top;
	while (n && *p == 0)
	{
		p++;
		n--;
	}
	if (!n)
		return 0;
	top = *p;
	n = (n - 1) * 8;
	while (top)
	{
		n++;
		top >>= 1;
	}
	return n;
}

int rdp_x509_parse(struct rdp_x509* c, const uint8_t* der, size_t len)
{
	struct der all, cert, tbs, tbsw, tmp, alg, oid, validity, t, spki, spkiw, bits, ext;
	int tag;

	if (!c || !der)
		return AMIPR_EINVAL;
	memset(c, 0, sizeof(*c));
	c->der = der;
	c->der_len = len;
	all.p = der;
	all.n = len;
	if (der_expect(&all, 0x30, &cert, NULL) != 0)
		return AMIPR_EINVAL;
	if (der_expect(&cert, 0x30, &tbs, &tbsw) != 0)
		return AMIPR_EINVAL;
	c->tbs = tbsw.p;
	c->tbs_len = tbsw.n;
	/* signatureAlgorithm, after the TBS */
	if (der_expect(&cert, 0x30, &alg, NULL) == 0 && der_expect(&alg, 0x06, &oid, NULL) == 0)
		c->sig_hash = sig_hash_of(&oid);

	(void)der_expect(&tbs, 0xa0, &tmp, NULL); /* [0] version */
	if (der_expect(&tbs, 0x02, &tmp, NULL) != 0)
		return AMIPR_EINVAL;
	c->serial = tmp.p;
	c->serial_len = tmp.n;
	if (der_expect(&tbs, 0x30, &tmp, NULL) != 0) /* signature */
		return AMIPR_EINVAL;
	if (der_expect(&tbs, 0x30, NULL, &tmp) != 0) /* issuer */
		return AMIPR_EINVAL;
	c->issuer = tmp.p;
	c->issuer_len = tmp.n;
	if (der_expect(&tbs, 0x30, &validity, NULL) != 0)
		return AMIPR_EINVAL;
	if (der_next(&validity, &tag, &t, NULL) == 0)
		copy_time(c->not_before, &t);
	if (der_next(&validity, &tag, &t, NULL) == 0)
		copy_time(c->not_after, &t);
	if (der_expect(&tbs, 0x30, NULL, &tmp) != 0) /* subject */
		return AMIPR_EINVAL;
	c->subject = tmp.p;
	c->subject_len = tmp.n;
	if (der_expect(&tbs, 0x30, &spki, &spkiw) != 0)
		return AMIPR_EINVAL;
	c->spki = spkiw.p;
	c->spki_len = spkiw.n;
	if (der_expect(&spki, 0x30, &alg, NULL) != 0 || der_expect(&alg, 0x06, &oid, NULL) != 0)
		return AMIPR_EINVAL;
	if (der_expect(&spki, 0x03, &bits, NULL) != 0 || bits.n < 1)
		return AMIPR_EINVAL;
	c->pubkey = bits.p + 1; /* skip the unused-bits count */
	c->pubkey_len = bits.n - 1;
	if (oid_is(&oid, OID_RSA_ENC, sizeof(OID_RSA_ENC)))
	{
		struct der key, rsa, n, e;
		key.p = c->pubkey;
		key.n = c->pubkey_len;
		if (der_expect(&key, 0x30, &rsa, NULL) == 0 && der_expect(&rsa, 0x02, &n, NULL) == 0 &&
		    der_expect(&rsa, 0x02, &e, NULL) == 0)
		{
			c->key_rsa = 1;
			c->rsa_n = n.p;
			c->rsa_n_len = n.n;
			c->rsa_e = e.p;
			c->rsa_e_len = e.n;
			c->key_bits = (uint32_t)int_bits(n.p, n.n);
		}
	}
	else
		c->key_bits = (uint32_t)(c->pubkey_len > 1 ? ((c->pubkey_len - 1) / 2) * 8 : 0);

	/* optional [1], [2], then [3] extensions */
	(void)der_expect(&tbs, 0x81, NULL, NULL);
	(void)der_expect(&tbs, 0x82, NULL, NULL);
	if (der_expect(&tbs, 0xa3, &ext, NULL) == 0)
	{
		struct der list;
		if (der_expect(&ext, 0x30, &list, NULL) == 0)
		{
			struct der one;
			while (der_expect(&list, 0x30, &one, NULL) == 0)
			{
				struct der eoid, val;
				if (der_expect(&one, 0x06, &eoid, NULL) != 0)
					continue;
				(void)der_expect(&one, 0x01, NULL, NULL); /* critical */
				if (der_expect(&one, 0x04, &val, NULL) != 0)
					continue;
				if (oid_is(&eoid, OID_SAN, sizeof(OID_SAN)))
				{
					c->san = val.p;
					c->san_len = val.n;
				}
			}
		}
	}
	return AMIPR_OK;
}

struct attr_name
{
	uint8_t oid[10];
	size_t n;
	const char* label;
};

static const struct attr_name s_names[] = {
	{ { 0x55, 0x04, 0x03 }, 3, "CN" },
	{ { 0x55, 0x04, 0x06 }, 3, "C" },
	{ { 0x55, 0x04, 0x07 }, 3, "L" },
	{ { 0x55, 0x04, 0x08 }, 3, "ST" },
	{ { 0x55, 0x04, 0x0a }, 3, "O" },
	{ { 0x55, 0x04, 0x0b }, 3, "OU" },
	{ { 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x09, 0x01 }, 9, "emailAddress" },
	{ { 0x09, 0x92, 0x26, 0x89, 0x93, 0xf2, 0x2c, 0x64, 0x01, 0x19 }, 10, "DC" },
};

static int append(char* buf, size_t size, size_t* used, const char* s, size_t n)
{
	if (*used + n + 1 > size)
		return -1;
	memcpy(buf + *used, s, n);
	*used += n;
	buf[*used] = '\0';
	return 0;
}

int rdp_x509_name_string(const uint8_t* name, size_t len, char* buf, size_t size)
{
	struct der d, seq, set;
	size_t used = 0;
	int first = 1;

	if (!name || !buf || size == 0)
		return AMIPR_EINVAL;
	buf[0] = '\0';
	d.p = name;
	d.n = len;
	if (der_expect(&d, 0x30, &seq, NULL) != 0)
		return AMIPR_EINVAL;
	while (der_expect(&seq, 0x31, &set, NULL) == 0)
	{
		struct der atv;
		while (der_expect(&set, 0x30, &atv, NULL) == 0)
		{
			struct der oid, val;
			int tag;
			const char* label = NULL;
			char numeric[40];
			size_t i;
			if (der_expect(&atv, 0x06, &oid, NULL) != 0 || der_next(&atv, &tag, &val, NULL) != 0)
				continue;
			for (i = 0; i < sizeof(s_names) / sizeof(s_names[0]); i++)
				if (oid.n == s_names[i].n && memcmp(oid.p, s_names[i].oid, oid.n) == 0)
				{
					label = s_names[i].label;
					break;
				}
			if (!label)
			{
				snprintf(numeric, sizeof(numeric), "OID.%u", oid.n ? (unsigned)oid.p[oid.n - 1] : 0u);
				label = numeric;
			}
			if ((!first && append(buf, size, &used, ", ", 2) != 0) ||
			    append(buf, size, &used, label, strlen(label)) != 0 ||
			    append(buf, size, &used, " = ", 3) != 0 ||
			    append(buf, size, &used, (const char*)val.p, val.n) != 0)
				return AMIPR_EINVAL;
			first = 0;
		}
	}
	return AMIPR_OK;
}

int rdp_x509_common_name(const struct rdp_x509* c, char* buf, size_t size)
{
	struct der d, seq, set, atv, oid, val;
	int tag, found = 0;

	if (!c || !buf || size == 0 || !c->subject)
		return AMIPR_EINVAL;
	d.p = c->subject;
	d.n = c->subject_len;
	if (der_expect(&d, 0x30, &seq, NULL) != 0)
		return AMIPR_EINVAL;
	/* the most specific (last) CN wins, as RFC 6125 6.4.4 suggests */
	while (der_expect(&seq, 0x31, &set, NULL) == 0)
		while (der_expect(&set, 0x30, &atv, NULL) == 0)
			if (der_expect(&atv, 0x06, &oid, NULL) == 0 && oid_is(&oid, OID_CN, 3) &&
			    der_next(&atv, &tag, &val, NULL) == 0 && val.n < size)
			{
				memcpy(buf, val.p, val.n);
				buf[val.n] = '\0';
				found = 1;
			}
	return found ? AMIPR_OK : AMIPR_EINVAL;
}

int rdp_x509_foreach_san(const struct rdp_x509* c, rdp_san_cb cb, void* user)
{
	struct der d, names, val;
	int tag;
	if (!c || !cb)
		return AMIPR_EINVAL;
	if (!c->san)
		return AMIPR_OK;
	d.p = c->san;
	d.n = c->san_len;
	if (der_expect(&d, 0x30, &names, NULL) != 0)
		return AMIPR_EINVAL;
	while (der_next(&names, &tag, &val, NULL) == 0)
	{
		int type = tag & 0x1f;
		if ((tag & 0xc0) != 0x80)
			continue;
		if (cb(user, type, val.p, val.n))
			break;
	}
	return AMIPR_OK;
}

static int lower(int ch)
{
	return (ch >= 'A' && ch <= 'Z') ? ch + 32 : ch;
}

static int name_eq(const char* a, size_t an, const char* b, size_t bn)
{
	size_t i;
	if (an != bn)
		return 0;
	for (i = 0; i < an; i++)
		if (lower((unsigned char)a[i]) != lower((unsigned char)b[i]))
			return 0;
	return 1;
}

/* RFC 6125 6.4.3: a "*" only as the whole left-most label */
static int dns_match(const char* pattern, size_t pn, const char* host)
{
	size_t hn = strlen(host);
	if (pn && pattern[pn - 1] == '.')
		pn--;
	if (hn && host[hn - 1] == '.')
		hn--;
	if (pn > 2 && pattern[0] == '*' && pattern[1] == '.')
	{
		const char* dot = memchr(host, '.', hn);
		if (!dot || dot == host)
			return 0;
		return name_eq(pattern + 1, pn - 1, dot, hn - (size_t)(dot - host));
	}
	return name_eq(pattern, pn, host, hn);
}

struct match_ctx
{
	const char* host;
	uint8_t ip[4];
	int is_ip;
	int saw_dns;
	int matched;
};

static int is_ipv4(const char* s, uint8_t out[4])
{
	unsigned v[4];
	char tail;
	int i;
	if (sscanf(s, "%u.%u.%u.%u%c", &v[0], &v[1], &v[2], &v[3], &tail) != 4)
		return 0;
	for (i = 0; i < 4; i++)
	{
		if (v[i] > 255)
			return 0;
		out[i] = (uint8_t)v[i];
	}
	return 1;
}

static int san_match_cb(void* user, int type, const uint8_t* value, size_t len)
{
	struct match_ctx* m = user;
	if (type == 2)
	{
		m->saw_dns = 1;
		if (!m->is_ip && dns_match((const char*)value, len, m->host))
			m->matched = 1;
	}
	else if (type == 7 && m->is_ip && len == 4 && memcmp(value, m->ip, 4) == 0)
		m->matched = 1;
	return m->matched;
}

int rdp_x509_match_host(const struct rdp_x509* c, const char* host)
{
	struct match_ctx m;
	char cn[256];

	if (!c || !host)
		return 0;
	memset(&m, 0, sizeof(m));
	m.host = host;
	m.is_ip = is_ipv4(host, m.ip);
	(void)rdp_x509_foreach_san(c, san_match_cb, &m);
	if (m.matched)
		return 1;
	/* the subject CN only counts when there are no DNS names (RFC 6125 6.4.4) */
	if (!m.saw_dns && !m.is_ip && rdp_x509_common_name(c, cn, sizeof(cn)) == AMIPR_OK)
		return dns_match(cn, strlen(cn), host);
	return 0;
}

int rdp_x509_channel_binding(const struct rdp_x509* c, uint8_t* out, size_t size, int* alg)
{
	struct amipr_hash h;
	int a, rc;
	if (!c || !out)
		return AMIPR_EINVAL;
	/* RFC 5929 4.1: MD5 and SHA-1 are replaced by SHA-256 */
	a = c->sig_hash;
	if (a == 0 || a == AMIPR_SHA1)
		a = AMIPR_SHA256;
	if ((rc = amipr_hash_init(&h, a)) != AMIPR_OK ||
	    (rc = amipr_hash_update(&h, c->der, c->der_len)) != AMIPR_OK)
		return rc;
	rc = amipr_hash_final(&h, out, size);
	if (rc > 0 && alg)
		*alg = a;
	return rc;
}
