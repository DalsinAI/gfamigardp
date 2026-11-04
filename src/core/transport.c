/*
 * GFAmigaRDP: the TCP and TLS byte stream.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 *
 * The socket is non-blocking; a call that would block waits on it with
 * amipr_wait (WaitSelect) for at most timeout_us, so a dead server
 * cannot hang the task. TLS records travel over the same socket through
 * gfopentls.library's I/O hook (amipr_tls_new's io callback below).
 */
#include "transport.h"
#include "x224.h"
#include "amipr.h"

#include <string.h>

void rdp_transport_init(struct rdp_transport* t)
{
	memset(t, 0, sizeof(*t));
	t->sock = -1;
	t->timeout_us = 15000000; /* 15 s */
}

static int wait_sock(struct rdp_transport* t, unsigned what)
{
	struct amipr_wait_item it;
	int rc;

	it.fd = t->sock;
	it.events = what;
	it.revents = 0;
	rc = amipr_wait(&it, 1, t->timeout_us);
	if (rc == 0)
		return AMIPR_ETIMEOUT;
	return rc < 0 ? rc : 0;
}

int rdp_transport_connect(struct rdp_transport* t, const char* host, uint16_t port,
                          int64_t timeout_us)
{
	int s;

	if (!host || strlen(host) >= sizeof(t->host))
		return RDP_E_ARG;
	s = amipr_tcp_connect(host, port, timeout_us);
	if (s < 0)
		return s;
	t->sock = s;
	strcpy(t->host, host);
	return 0;
}

/* the raw socket, for TLS underneath and for the stream before it */
static int raw_io(struct rdp_transport* t, int write, void* buf, int len)
{
	return write ? amipr_tcp_send(t->sock, buf, len) : amipr_tcp_recv(t->sock, buf, len);
}

static int tls_io(void* user, int write, void* buf, int len)
{
	/* non-blocking below: gfopentls gets AMIPR_EAGAIN as OTERR_WOULDBLOCK
	 * and we wait in the loops here */
	return raw_io((struct rdp_transport*)user, write, buf, len);
}

int rdp_transport_start_tls(struct rdp_transport* t)
{
	int rc;

	if (t->sock < 0 || t->tls)
		return RDP_E_ARG;
	t->tls = amipr_tls_new(t->host, tls_io, t);
	if (!t->tls)
		return AMIPR_ENOLIB;
	for (;;)
	{
		rc = amipr_tls_handshake(t->tls);
		if (rc != AMIPR_EAGAIN)
			return rc;
		/* it wants bytes or room: either way the socket is the thing to wait on */
		rc = wait_sock(t, AMIPR_WAIT_READ | AMIPR_WAIT_WRITE);
		if (rc < 0)
			return rc;
	}
}

static int io_once(struct rdp_transport* t, int write, void* buf, int len)
{
	if (t->tls)
		return write ? amipr_tls_write(t->tls, buf, len) : amipr_tls_read(t->tls, buf, len);
	return raw_io(t, write, buf, len);
}

int rdp_transport_write(struct rdp_transport* t, const void* buf, size_t len)
{
	const uint8_t* p = buf;

	while (len > 0)
	{
		int chunk = len > 0x4000 ? 0x4000 : (int)len;
		int n = io_once(t, 1, (void*)p, chunk);
		if (n == AMIPR_EAGAIN)
		{
			int rc = wait_sock(t, t->tls ? AMIPR_WAIT_READ | AMIPR_WAIT_WRITE : AMIPR_WAIT_WRITE);
			if (rc < 0)
				return rc;
			continue;
		}
		if (n <= 0)
			return n < 0 ? n : AMIPR_ECLOSED;
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

int rdp_transport_read_full(struct rdp_transport* t, void* buf, size_t len)
{
	uint8_t* p = buf;

	while (len > 0)
	{
		int n = io_once(t, 0, p, len > 0x4000 ? 0x4000 : (int)len);
		if (n == AMIPR_EAGAIN)
		{
			int rc = wait_sock(t, AMIPR_WAIT_READ);
			if (rc < 0)
				return rc;
			continue;
		}
		if (n == 0 || n == AMIPR_ECLOSED)
			return AMIPR_ECLOSED;
		if (n < 0)
			return n;
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

int rdp_transport_read_tpkt(struct rdp_transport* t, uint8_t* buf, size_t size)
{
	int rc, len;

	if (size < RDP_TPKT_HEADER)
		return RDP_E_SPACE;
	rc = rdp_transport_read_full(t, buf, RDP_TPKT_HEADER);
	if (rc < 0)
		return rc;
	/* a fast-path PDU starts with action bits 0, not 3: not expected
	 * before the connection is up, so it is a protocol error here */
	len = rdp_tpkt_length(buf);
	if (len < 0)
		return len;
	if ((size_t)len > size)
		return RDP_E_SPACE;
	rc = rdp_transport_read_full(t, buf + RDP_TPKT_HEADER, (size_t)len - RDP_TPKT_HEADER);
	return rc < 0 ? rc : len;
}

void rdp_transport_close(struct rdp_transport* t)
{
	if (t->tls)
	{
		amipr_tls_close(t->tls);
		amipr_tls_free(t->tls);
		t->tls = NULL;
	}
	if (t->sock >= 0)
	{
		amipr_tcp_close(t->sock);
		t->sock = -1;
	}
}
