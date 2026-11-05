/*
 * amipr: bsdsocket.library: the TCP connection and name lookup.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 *
 * The calls go straight to bsdsocket.library (version 4, the AmiTCP
 * interface every Amiga stack offers), not through the C library's socket
 * emulation. A base belongs to the task that opened it, so each task opens
 * its own (amipr_net_open) and keeps it with its other resources; its
 * sockets are used from that task only. amipr_tcp_release() and
 * amipr_tcp_obtain() hand one to another task (ReleaseSocket() and
 * ObtainSocket(), bsdsocket.library v4): the connection task makes the
 * connection, the session window then owns it.
 *
 * gethostbyname() blocks the task while the stack asks a name server: the
 * connection window shows "Looking up the host..." before it is called.
 */
#include "amipr_internal.h"

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/filio.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <proto/bsdsocket.h>
#include <libraries/bsdsocket.h>
#include <utility/tagitem.h>

#ifndef FIONBIO
#define FIONBIO _IOW('f', 126, long)
#endif
#ifndef EWOULDBLOCK
#define EWOULDBLOCK 35
#endif
#ifndef EINPROGRESS
#define EINPROGRESS 36
#endif

/* the calls name their base SocketBase: give them this task's */
#define SocketBase ((struct Library*)amipr_socket_base())

void* amipr_socket_base(void)
{
	struct amipr_task_res* r = amipr_task_res();
	return r ? r->socket_base : NULL;
}

int amipr_net_open(void)
{
	struct amipr_task_res* r = amipr_task_res();

	if (!r)
		return AMIPR_ENOLIB;
	if (!r->socket_base)
		r->socket_base = OpenLibrary((CONST_STRPTR) "bsdsocket.library", 4);
	return r->socket_base ? AMIPR_OK : AMIPR_ENOLIB;
}

void amipr_net_close(void)
{
	struct amipr_task_res* r = amipr_task_res();

	if (r && r->socket_base)
	{
		CloseLibrary(r->socket_base);
		r->socket_base = NULL;
	}
}

int amipr_net_error(void)
{
	return SocketBase ? (int)Errno() : 0;
}

int amipr_inet_pton4(const char* s, uint8_t out[4])
{
	struct in_addr a;
	int dots = 0;
	const char* p;

	if (!s || !out || !SocketBase)
		return 0;
	/* inet_aton also takes "10.1" and hex forms; the presentation format
	 * is exactly four decimal parts */
	for (p = s; *p; p++)
	{
		if (*p == '.')
			dots++;
		else if (*p < '0' || *p > '9')
			return 0;
	}
	if (dots != 3 || !inet_aton((STRPTR)s, &a))
		return 0;
	memcpy(out, &a.s_addr, 4); /* network order is the 68k's order */
	return 1;
}

int amipr_inet_ntop4(const uint8_t in[4], char* buf, size_t size)
{
	struct in_addr a;
	const char* s;
	size_t n;

	if (!in || !buf || !SocketBase)
		return AMIPR_EINVAL;
	memcpy(&a.s_addr, in, 4);
	s = (const char*)Inet_NtoA(a.s_addr);
	n = s ? strlen(s) : 0;
	if (!s || n + 1 > size)
		return AMIPR_EINVAL;
	memcpy(buf, s, n + 1);
	return AMIPR_OK;
}

int amipr_host_name(char* buf, size_t size)
{
	if (!buf || size == 0)
		return AMIPR_EINVAL;
	if (SocketBase && gethostname((STRPTR)buf, (LONG)size) == 0)
	{
		buf[size - 1] = '\0';
		return AMIPR_OK;
	}
	if (amipr_getenv("HOSTNAME", buf, size) > 0)
		return AMIPR_OK;
	if (size < 6)
		return AMIPR_EINVAL;
	strcpy(buf, "amiga");
	return AMIPR_OK;
}

static int set_nonblocking(int s)
{
	long on = 1;
	return IoctlSocket(s, FIONBIO, (char*)&on) == 0 ? AMIPR_OK : AMIPR_EINVAL;
}

int amipr_tcp_connect(const char* host, uint16_t port, int64_t timeout_us)
{
	struct sockaddr_in sa;
	uint8_t ip[4];
	int s;
	LONG one = 1;

	if (!host || !*host)
		return AMIPR_EINVAL;
	if (amipr_net_open() != AMIPR_OK)
		return AMIPR_ENOLIB;

	memset(&sa, 0, sizeof(sa));
	sa.sin_len = sizeof(sa);
	sa.sin_family = AF_INET;
	sa.sin_port = port; /* the 68k is big-endian: host order is network order */
	if (amipr_inet_pton4(host, ip))
		memcpy(&sa.sin_addr.s_addr, ip, 4);
	else
	{
		/* a slow name server holds gethostbyname, which does not wait in
		 * amipr_wait; the abort hook's signals (the connection task's
		 * port: Cancel) are added to bsdsocket's break mask for it, so
		 * the stack breaks the lookup off as it does on Ctrl-C (review 2,
		 * left from M2). A stack whose resolver does not look at the
		 * mask still ends the lookup at its own timeout. */
		uint32_t abort = amipr_abort_sigmask();
		ULONG mask = SIGBREAKF_CTRL_C;
		struct hostent* he;
		if (abort && SocketBaseTags(SBTM_GETREF(SBTC_BREAKMASK), (ULONG)&mask, TAG_END) == 0)
			SocketBaseTags(SBTM_SETVAL(SBTC_BREAKMASK), mask | abort, TAG_END);
		else
			abort = 0;
		he = gethostbyname((STRPTR)host);
		if (abort)
		{
			SocketBaseTags(SBTM_SETVAL(SBTC_BREAKMASK), mask, TAG_END);
			if (amipr_abort_check())
				return AMIPR_ECANCEL;
		}
		if (!he || he->h_addrtype != AF_INET || !he->h_addr_list || !he->h_addr_list[0])
			return AMIPR_ENOTFOUND;
		memcpy(&sa.sin_addr.s_addr, he->h_addr_list[0], 4);
	}

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		return AMIPR_ENET;
	/* RDP sends many small PDUs (input): no Nagle delay */
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	if (set_nonblocking(s) != AMIPR_OK)
	{
		CloseSocket(s);
		return AMIPR_ENET;
	}
	if (connect(s, (struct sockaddr*)&sa, sizeof(sa)) < 0)
	{
		LONG e = Errno();
		struct amipr_wait_item it;
		LONG err = 0, len = sizeof(err);
		int rc;

		if (e != EINPROGRESS && e != EWOULDBLOCK)
		{
			CloseSocket(s);
			return AMIPR_ENET;
		}
		it.fd = s;
		it.events = AMIPR_WAIT_WRITE;
		it.revents = 0;
		rc = amipr_wait(&it, 1, timeout_us);
		if (rc == 0)
		{
			CloseSocket(s);
			return AMIPR_ETIMEOUT;
		}
		if (rc < 0 || getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0)
		{
			CloseSocket(s);
			return AMIPR_ENET;
		}
	}
	return s;
}

int amipr_tcp_send(int s, const void* buf, int len)
{
	LONG n;

	if (!SocketBase || s < 0)
		return AMIPR_EINVAL;
	n = send(s, (APTR)buf, len, 0);
	if (n >= 0)
		return (int)n;
	return Errno() == EWOULDBLOCK ? AMIPR_EAGAIN : AMIPR_ENET;
}

int amipr_tcp_recv(int s, void* buf, int len)
{
	LONG n;

	if (!SocketBase || s < 0)
		return AMIPR_EINVAL;
	n = recv(s, buf, len, 0);
	if (n >= 0)
		return (int)n;
	return Errno() == EWOULDBLOCK ? AMIPR_EAGAIN : AMIPR_ENET;
}

void amipr_tcp_close(int s)
{
	if (SocketBase && s >= 0)
		CloseSocket(s);
}

#ifndef UNIQUE_ID
#define UNIQUE_ID (-1)
#endif

long amipr_tcp_release(int s)
{
	if (!SocketBase || s < 0)
		return -1;
	return (long)ReleaseSocket(s, UNIQUE_ID);
}

int amipr_tcp_obtain(long id)
{
	LONG s;
	if (id < 0 || amipr_net_open() != AMIPR_OK)
		return AMIPR_ENET;
	s = ObtainSocket(id, AF_INET, SOCK_STREAM, 0);
	return s >= 0 ? (int)s : AMIPR_ENET;
}
