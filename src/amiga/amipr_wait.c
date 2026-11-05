/*
 * amipr: waitable objects and waiting on them together with sockets.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 *
 * Design
 *  - Every waitable object (wobj) is a counter plus a list of the tasks
 *    waiting on it. One SignalSemaphore guards all of them: the sections
 *    it protects are a few instructions long.
 *  - A waiting task owns one exec signal bit (amipr_task_res()). It
 *    clears that bit, hangs a node on every wobj it waits for, and then
 *    sleeps in Wait() - or in bsdsocket's WaitSelect() when sockets are in
 *    the set, which returns on socket activity or on that same signal.
 *    Whoever raises a counter Signal()s each waiting task. A signal that
 *    arrives between the check and the sleep stays pending in the task's
 *    signal mask, so it cannot be lost.
 *  - Timers are wobjs with a due time on the E-clock. Waiters evaluate the
 *    due time when they look at the object and include it in their sleep
 *    deadline, so no timer task is needed.
 *  - Deadlines use the task's own timerequest (Wait path) or WaitSelect's
 *    timeout (socket path).
 */
#include "amipr_internal.h"

#include <string.h>
#include <clib/alib_protos.h> /* NewList (amiga.lib) */
#include <sys/socket.h> /* OpenNDK: fd_set for WaitSelect */
#include <proto/bsdsocket.h>

struct amipr_waiter
{
	struct MinNode node;
	struct Task* task;
	ULONG mask;
};

struct amipr_wobj
{
	uint32_t count;
	uint64_t due;     /* E-clock microseconds; 0 not armed */
	uint64_t period;  /* 0 one-shot */
	struct MinList waiters;
};

/* Objects are allocated one by one so they never move (their waiter
 * lists point back at them); the table of pointers may grow. */
static struct SignalSemaphore s_lock;
static volatile int s_lock_ready;
static struct amipr_wobj** s_objs;
static int s_nobjs;

static void lock_init(void)
{
	if (s_lock_ready)
		return;
	Forbid();
	if (!s_lock_ready)
	{
		InitSemaphore(&s_lock);
		s_lock_ready = 1;
	}
	Permit();
}

static struct amipr_wobj* obj_get(int h)
{
	int i = h - AMIPR_WOBJ_BASE;
	if (i < 0 || i >= s_nobjs)
		return NULL;
	return s_objs[i];
}

static void obj_wake(struct amipr_wobj* o)
{
	struct amipr_waiter* w;
	for (w = (struct amipr_waiter*)o->waiters.mlh_Head; w->node.mln_Succ;
	     w = (struct amipr_waiter*)w->node.mln_Succ)
		Signal(w->task, w->mask);
}

/* called with s_lock held */
static void obj_eval_due(struct amipr_wobj* o, uint64_t now)
{
	if (!o->due || now < o->due)
		return;
	if (o->count == 0)
		o->count = 1;
	if (o->period)
	{
		uint64_t missed = (now - o->due) / o->period;
		o->due += (missed + 1) * o->period;
	}
	else
		o->due = 0;
}

int amipr_is_wobj(int fd)
{
	return fd >= AMIPR_WOBJ_BASE && fd < AMIPR_WOBJ_BASE + 0x100000;
}

int amipr_wobj_new(uint32_t initial_count)
{
	struct amipr_wobj* o;
	int i;

	if (amipr_init() != AMIPR_OK)
		return AMIPR_ENOLIB;
	lock_init();
	o = AllocVec(sizeof(*o), MEMF_ANY | MEMF_CLEAR);
	if (!o)
		return AMIPR_ENOMEM;
	o->count = initial_count;
	NewList((struct List*)&o->waiters);

	ObtainSemaphore(&s_lock);
	for (i = 0; i < s_nobjs; i++)
		if (!s_objs[i])
			break;
	if (i == s_nobjs)
	{
		int n = s_nobjs ? s_nobjs * 2 : 64;
		struct amipr_wobj** grown;
		if (n > 0x100000 || !(grown = AllocVec(sizeof(*grown) * n, MEMF_ANY | MEMF_CLEAR)))
		{
			ReleaseSemaphore(&s_lock);
			FreeVec(o);
			return AMIPR_ENOMEM;
		}
		if (s_objs)
		{
			CopyMem(s_objs, grown, sizeof(*grown) * s_nobjs);
			FreeVec(s_objs);
		}
		s_objs = grown;
		s_nobjs = n;
	}
	s_objs[i] = o;
	ReleaseSemaphore(&s_lock);
	return AMIPR_WOBJ_BASE + i;
}

void amipr_wobj_free(int h)
{
	struct amipr_wobj* o;

	if (!s_lock_ready)
		return;
	ObtainSemaphore(&s_lock);
	o = obj_get(h);
	if (o)
	{
		/* anybody still waiting is woken and finds the handle gone */
		obj_wake(o);
		s_objs[h - AMIPR_WOBJ_BASE] = NULL;
	}
	ReleaseSemaphore(&s_lock);
	/* a waiter may still have its node on o->waiters: it removes the node
	 * under s_lock before touching anything else, after looking the
	 * handle up again, so o is only freed once nobody is linked to it */
	if (o)
	{
		for (;;)
		{
			int busy;
			ObtainSemaphore(&s_lock);
			busy = o->waiters.mlh_Head->mln_Succ != NULL;
			ReleaseSemaphore(&s_lock);
			if (!busy)
				break;
			amipr_sleep_us(1000);
		}
		FreeVec(o);
	}
}

static int with_obj(int h, int (*fn)(struct amipr_wobj*, void*), void* arg)
{
	struct amipr_wobj* o;
	int rc;

	if (!s_lock_ready)
		return AMIPR_EINVAL;
	ObtainSemaphore(&s_lock);
	o = obj_get(h);
	rc = o ? fn(o, arg) : AMIPR_EINVAL;
	ReleaseSemaphore(&s_lock);
	return rc;
}

static int do_set(struct amipr_wobj* o, void* arg)
{
	(void)arg;
	if (o->count == 0)
		o->count = 1;
	obj_wake(o);
	return AMIPR_OK;
}

int amipr_wobj_set(int h)
{
	return with_obj(h, do_set, NULL);
}

struct add_args
{
	uint32_t n, limit;
	uint32_t* previous;
};

static int do_add(struct amipr_wobj* o, void* arg)
{
	struct add_args* a = arg;
	if (a->previous)
		*a->previous = o->count;
	if (a->limit && (a->n > a->limit || o->count > a->limit - a->n))
		return AMIPR_EINVAL;
	o->count += a->n;
	obj_wake(o);
	return AMIPR_OK;
}

int amipr_wobj_add(int h, uint32_t n, uint32_t limit, uint32_t* previous)
{
	struct add_args a;
	a.n = n;
	a.limit = limit;
	a.previous = previous;
	return with_obj(h, do_add, &a);
}

static int do_reset(struct amipr_wobj* o, void* arg)
{
	(void)arg;
	o->count = 0;
	return AMIPR_OK;
}

int amipr_wobj_reset(int h)
{
	return with_obj(h, do_reset, NULL);
}

static int do_take(struct amipr_wobj* o, void* arg)
{
	obj_eval_due(o, *(uint64_t*)arg);
	if (o->count == 0)
		return 0;
	o->count--;
	return 1;
}

int amipr_wobj_take(int h)
{
	uint64_t now = amipr_monotonic_us();
	return with_obj(h, do_take, &now);
}

static int do_count(struct amipr_wobj* o, void* arg)
{
	obj_eval_due(o, *(uint64_t*)arg);
	return (int)(o->count > 0x7fffffffUL ? 0x7fffffffUL : o->count);
}

int amipr_wobj_count(int h)
{
	uint64_t now = amipr_monotonic_us();
	return with_obj(h, do_count, &now);
}

struct arm_args
{
	uint64_t due, period;
};

static int do_arm(struct amipr_wobj* o, void* arg)
{
	struct arm_args* a = arg;
	o->due = a->due;
	o->period = a->period;
	o->count = 0;
	/* waiters recompute their deadline */
	obj_wake(o);
	return AMIPR_OK;
}

int amipr_wobj_arm(int h, uint64_t due_monotonic_us, uint64_t period_us)
{
	struct arm_args a;
	a.due = due_monotonic_us;
	a.period = period_us;
	return with_obj(h, do_arm, &a);
}

/* ------------------------------------------------------------------ */

#define MAX_STACK_ITEMS 64

/* 1: a wobj; 2: a socket of this task's bsdsocket base */
static int fd_kind(int fd, int* sock)
{
	if (amipr_is_wobj(fd))
		return 1;
	if (fd >= 0)
	{
		*sock = fd;
		return 2;
	}
	return 0;
}

/* the task's abort hook (amipr_set_abort): one per program, used only
 * from the task that set it */
static ULONG s_abort_sigs;
static amipr_abort_cb s_abort_cb;
static void* s_abort_user;
static struct Task* s_abort_task;
static int s_in_abort;

void amipr_set_abort(uint32_t sigmask, amipr_abort_cb cb, void* user)
{
	s_abort_sigs = cb ? (ULONG)sigmask : 0;
	s_abort_cb = cb;
	s_abort_user = user;
	s_abort_task = cb ? FindTask(NULL) : NULL;
}

uint32_t amipr_abort_sigmask(void)
{
	return (s_abort_cb && s_abort_task == FindTask(NULL)) ? (uint32_t)s_abort_sigs : 0;
}

int amipr_abort_check(void)
{
	ULONG came;
	int stop;
	if (!s_abort_cb || s_in_abort || s_abort_task != FindTask(NULL))
		return 0;
	came = SetSignal(0, s_abort_sigs) & s_abort_sigs;
	s_in_abort = 1;
	stop = s_abort_cb(s_abort_user, (uint32_t)came);
	s_in_abort = 0;
	return stop;
}

/* extra: signals to wake on as well; *got: those that came (NULL: the
 * abort hook's, which are not handed back but given to the hook) */
static int wait_impl(struct amipr_wait_item* items, size_t count, int64_t timeout_us, ULONG extra, ULONG* got)
{
	struct amipr_waiter stack_nodes[MAX_STACK_ITEMS];
	struct amipr_waiter* nodes = stack_nodes;
	struct amipr_task_res* r = amipr_task_res();
	struct Library* SocketBase = amipr_socket_base();
	uint64_t start = amipr_monotonic_us();
	uint64_t deadline = timeout_us < 0 ? 0 : start + (uint64_t)timeout_us;
	int rc = 0;
	size_t i;

	if (!r || r->wake_sig < 0)
		return AMIPR_ENOLIB;
	if (count > MAX_STACK_ITEMS)
	{
		nodes = AllocVec(sizeof(*nodes) * count, MEMF_ANY | MEMF_CLEAR);
		if (!nodes)
			return AMIPR_ENOMEM;
	}
	lock_init();

	for (;;)
	{
		const ULONG wake = 1UL << r->wake_sig;
		ULONG abort_sigs = (!got && s_abort_cb && !s_in_abort && s_abort_task == FindTask(NULL)) ? s_abort_sigs : 0;
		ULONG others = (extra | abort_sigs) & ~wake;
		ULONG came = 0;
		uint64_t now, wake_at = deadline;
		fd_set rset, wset;
		int maxsock = -1;
		int ready = 0;
		int linked = 0;

		FD_ZERO(&rset);
		FD_ZERO(&wset);
		SetSignal(0, wake);

		now = amipr_monotonic_us();
		ObtainSemaphore(&s_lock);
		for (i = 0; i < count; i++)
		{
			int sock = -1;
			int kind = fd_kind(items[i].fd, &sock);
			items[i].revents = 0;
			nodes[i].task = NULL;
			if (kind == 1)
			{
				struct amipr_wobj* o = obj_get(items[i].fd);
				if (!o)
				{
					rc = AMIPR_EINVAL;
					break;
				}
				obj_eval_due(o, now);
				if (o->count > 0)
				{
					items[i].revents = items[i].events & AMIPR_WAIT_READ;
					if (!items[i].revents)
						items[i].revents = AMIPR_WAIT_READ;
					ready++;
				}
				else
				{
					if (o->due && (wake_at == 0 || o->due < wake_at))
						wake_at = o->due;
					nodes[i].task = FindTask(NULL);
					nodes[i].mask = wake;
					AddTail((struct List*)&o->waiters, (struct Node*)&nodes[i].node);
					linked = 1;
				}
			}
			else if (kind == 2 && SocketBase)
			{
				if (items[i].events & AMIPR_WAIT_READ)
					FD_SET(sock, &rset);
				if (items[i].events & AMIPR_WAIT_WRITE)
					FD_SET(sock, &wset);
				if (sock > maxsock)
					maxsock = sock;
			}
			else
			{
				rc = AMIPR_EINVAL;
				break;
			}
		}
		ReleaseSemaphore(&s_lock);

		if (rc >= 0)
		{
			int sleep = (ready == 0);
			if (maxsock >= 0)
			{
				struct timeval tv;
				ULONG sigs = wake | others;
				LONG n;
				if (!sleep || (wake_at && wake_at <= now))
				{
					tv.tv_secs = 0;
					tv.tv_micro = 0;
				}
				else if (wake_at)
				{
					uint64_t d = wake_at - now;
					tv.tv_secs = (ULONG)(d / 1000000ULL);
					tv.tv_micro = (ULONG)(d % 1000000ULL);
				}
				n = __WaitSelect_base(SocketBase, maxsock + 1, &rset, &wset, NULL,
				                      (sleep && !wake_at) ? NULL : &tv, &sigs);
				came = sigs & others;
				if (n < 0)
					rc = AMIPR_EINVAL;
				else if (n > 0)
				{
					for (i = 0; i < count; i++)
					{
						int sock = -1;
						if (fd_kind(items[i].fd, &sock) != 2)
							continue;
						if (FD_ISSET(sock, &rset))
							items[i].revents |= AMIPR_WAIT_READ;
						if (FD_ISSET(sock, &wset))
							items[i].revents |= AMIPR_WAIT_WRITE;
						if (items[i].revents)
							ready++;
					}
				}
			}
			else if (sleep && !(wake_at && wake_at <= now))
			{
				if (wake_at)
				{
					uint64_t d = wake_at - now;
					ULONG tsig = 1UL << r->port->mp_SigBit;
					r->tr->tr_node.io_Command = TR_ADDREQUEST;
					r->tr->tr_time.tv_secs = (ULONG)(d / 1000000ULL);
					r->tr->tr_time.tv_micro = (ULONG)(d % 1000000ULL);
					SendIO((struct IORequest*)r->tr);
					came = Wait(wake | tsig | others) & others;
					if (!CheckIO((struct IORequest*)r->tr))
						AbortIO((struct IORequest*)r->tr);
					WaitIO((struct IORequest*)r->tr);
				}
				else
					came = Wait(wake | others) & others;
			}
			else if (others && got)
				came = SetSignal(0, others) & others;   /* nothing to sleep for: a look only */
		}

		if (linked)
		{
			ObtainSemaphore(&s_lock);
			for (i = 0; i < count; i++)
				if (nodes[i].task)
					Remove((struct Node*)&nodes[i].node);
			ReleaseSemaphore(&s_lock);
		}

		if (rc < 0 || ready > 0)
		{
			if (got)
				*got |= came;
			else if (came & abort_sigs)
				SetSignal(came & abort_sigs, came & abort_sigs);   /* kept for the next wait */
			break;
		}
		if (came && got)
		{
			*got |= came;
			break;               /* one of the caller's signals: rc 0, *got says which */
		}
		if (came & abort_sigs)
		{
			int stop;
			s_in_abort = 1;
			stop = s_abort_cb(s_abort_user, (uint32_t)(came & abort_sigs));
			s_in_abort = 0;
			if (stop)
			{
				rc = AMIPR_ECANCEL;
				break;
			}
		}
		if (deadline && amipr_monotonic_us() >= deadline)
			break; /* rc stays 0: timeout */
		/* woken by a signal or a due time: look again */
	}

	if (nodes != stack_nodes)
		FreeVec(nodes);
	if (rc < 0)
		return rc;
	rc = 0;
	for (i = 0; i < count; i++)
		if (items[i].revents)
			rc++;
	return rc;
}

int amipr_wait(struct amipr_wait_item* items, size_t count, int64_t timeout_us)
{
	return wait_impl(items, count, timeout_us, 0, NULL);
}

int amipr_wait_signals(struct amipr_wait_item* items, size_t count, int64_t timeout_us, uint32_t* sigs)
{
	ULONG got = 0;
	int rc;
	if (!sigs)
		return wait_impl(items, count, timeout_us, 0, NULL);
	rc = wait_impl(items, count, timeout_us, (ULONG)*sigs, &got);
	*sigs = (uint32_t)got;
	return rc;
}
