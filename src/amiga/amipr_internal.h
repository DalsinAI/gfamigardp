/* amipr internals. MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6). */
#ifndef AMIPR_INTERNAL_H
#define AMIPR_INTERNAL_H

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <exec/execbase.h>
#include <exec/lists.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>

#include "amipr.h"

extern struct Device* TimerBase;

/* opened once by amipr_init() (from the first task that calls in) */
extern struct Library* amipr_LocaleBase;
extern ULONG amipr_eclock_hz;

/* per-task resources, cached; amipr_thread_exit() gives them back */
struct amipr_task_res
{
	struct MinNode node;
	struct Task* task;
	struct Library* socket_base; /* bsdsocket.library, opened by this task */
	struct MsgPort* port;
	struct timerequest* tr;
	LONG wake_sig;   /* AllocSignal() bit, -1 none */
};
struct amipr_task_res* amipr_task_res(void);

/* Amiga epoch (1978-01-01) to Unix epoch (1970-01-01), in seconds */
#define AMIPR_EPOCH_DIFF 252460800L

#endif
