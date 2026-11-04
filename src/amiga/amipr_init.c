/*
 * amipr: library and device lifetime, per-task resources.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 *
 * timer.device is opened once on UNIT_MICROHZ by the first caller and kept
 * for the life of the program; its device base is what ReadEClock() and
 * GetSysTime() need, and those may be called from any task. Waiting tasks
 * each get their own MsgPort and timerequest (a message port belongs to a
 * task, so these cannot be shared). They are kept in a list keyed by the
 * task (FindTask(NULL)), not in thread-local storage, which AmigaOS has not.
 */
#include "amipr_internal.h"

#include <stdlib.h>

#include <proto/locale.h>
#include <clib/alib_protos.h>

struct Device* TimerBase;
struct Library* amipr_LocaleBase;
ULONG amipr_eclock_hz;

static struct MsgPort* s_main_port;
static struct timerequest* s_main_tr;
static struct SignalSemaphore s_init_lock;
static volatile int s_inited;

static struct MinList s_tasks;

/* the entry of the calling task, or NULL; s_init_lock held */
static struct amipr_task_res* find_res(struct Task* me)
{
	struct MinNode* n;
	for (n = s_tasks.mlh_Head; n->mln_Succ; n = n->mln_Succ)
		if (((struct amipr_task_res*)n)->task == me)
			return (struct amipr_task_res*)n;
	return NULL;
}

static void free_res(struct amipr_task_res* r);

static void amipr_shutdown(void)
{
	struct amipr_task_res* r;

	/* what is left: the main task's own entry, and any a task forgot */
	while ((r = (struct amipr_task_res*)RemHead((struct List*)&s_tasks)) != NULL)
		free_res(r);
	if (amipr_LocaleBase)
	{
		CloseLibrary(amipr_LocaleBase);
		amipr_LocaleBase = NULL;
	}
	if (s_main_tr)
	{
		if (TimerBase)
			CloseDevice((struct IORequest*)s_main_tr);
		DeleteIORequest((struct IORequest*)s_main_tr);
		s_main_tr = NULL;
		TimerBase = NULL;
	}
	if (s_main_port)
	{
		DeleteMsgPort(s_main_port);
		s_main_port = NULL;
	}
}

int amipr_init(void)
{
	struct EClockVal ev;

	if (s_inited > 0)
		return AMIPR_OK;

	/* The semaphore is set up under Forbid() by whoever comes first; all
	 * later callers then queue on it until the first one is done. */
	Forbid();
	if (s_inited == 0)
	{
		InitSemaphore(&s_init_lock);
		NewList((struct List*)&s_tasks);
		s_inited = -1;
	}
	Permit();

	ObtainSemaphore(&s_init_lock);
	if (s_inited > 0)
	{
		ReleaseSemaphore(&s_init_lock);
		return AMIPR_OK;
	}
	s_main_port = CreateMsgPort();
	if (s_main_port)
		s_main_tr = (struct timerequest*)CreateIORequest(s_main_port, sizeof(struct timerequest));
	if (!s_main_tr || OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest*)s_main_tr, 0) != 0)
	{
		amipr_shutdown();
		ReleaseSemaphore(&s_init_lock);
		return AMIPR_ENOLIB;
	}
	TimerBase = s_main_tr->tr_node.io_Device;
	amipr_eclock_hz = ReadEClock(&ev);
	/* locale.library is optional: without it the clock is taken as UTC */
	amipr_LocaleBase = OpenLibrary((CONST_STRPTR) "locale.library", 38);
	atexit(amipr_shutdown);
	s_inited = 1;
	ReleaseSemaphore(&s_init_lock);
	return AMIPR_OK;
}

struct amipr_task_res* amipr_task_res(void)
{
	struct Task* me = FindTask(NULL);
	struct amipr_task_res* r;

	if (amipr_init() != AMIPR_OK)
		return NULL;
	ObtainSemaphore(&s_init_lock);
	r = find_res(me);
	if (!r)
	{
		r = AllocVec(sizeof(*r), MEMF_ANY | MEMF_CLEAR);
		if (r)
		{
			r->task = me;
			r->wake_sig = -1;
			AddTail((struct List*)&s_tasks, (struct Node*)&r->node);
		}
	}
	ReleaseSemaphore(&s_init_lock);
	if (!r)
		return NULL;
	if (!r->port)
	{
		r->port = CreateMsgPort();
		if (!r->port)
			return NULL;
		r->tr = (struct timerequest*)CreateIORequest(r->port, sizeof(struct timerequest));
		if (!r->tr)
		{
			DeleteMsgPort(r->port);
			r->port = NULL;
			return NULL;
		}
		/* share the unit opened in amipr_init(): copy its device and unit */
		r->tr->tr_node.io_Device = s_main_tr->tr_node.io_Device;
		r->tr->tr_node.io_Unit = s_main_tr->tr_node.io_Unit;
	}
	if (r->wake_sig < 0)
		r->wake_sig = AllocSignal(-1);
	return r;
}

static void free_res(struct amipr_task_res* r)
{
	if (r->socket_base)
		CloseLibrary(r->socket_base);
	if (r->tr)
		DeleteIORequest((struct IORequest*)r->tr);
	if (r->port)
		DeleteMsgPort(r->port);
	if (r->wake_sig >= 0 && r->task == FindTask(NULL))
		FreeSignal(r->wake_sig);
	FreeVec(r);
}

void amipr_thread_exit(void)
{
	struct amipr_task_res* r;

	if (s_inited <= 0)
		return;
	ObtainSemaphore(&s_init_lock);
	r = find_res(FindTask(NULL));
	if (r)
		Remove((struct Node*)&r->node);
	ReleaseSemaphore(&s_init_lock);
	if (r)
		free_res(r);
}

const char* amipr_version(void)
{
	return "amipr 0.1 (" __DATE__ ")";
}

void* amipr_alloc(size_t size)
{
	return AllocVec(size ? size : 1, MEMF_ANY);
}

void amipr_free(void* p)
{
	if (p)
		FreeVec(p);
}
