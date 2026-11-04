/*
 * amipr: atomic operations.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 *
 * exec Disable()/Enable() around a read-modify-write: AmigaOS 3.x runs on
 * one CPU, so with interrupts off nothing else can run. That makes these
 * safe against other tasks AND against interrupt code, at the cost of a
 * few microseconds with interrupts masked. (The 68020+ CAS instruction is
 * not used: its locked read-modify-write bus cycle is not supported on
 * chip RAM and on some accelerator buses.)
 *
 * GCC on m68k-amigaos turns __atomic/__sync builtins into calls to the
 * __sync_*_4 / __atomic_*_4 functions below, so code using the compiler
 * builtins also ends up here.
 */
#include "amipr_internal.h"

int32_t amipr_atomic_add(volatile int32_t* p, int32_t v)
{
	int32_t r;
	Disable();
	r = *p + v;
	*p = r;
	Enable();
	return r;
}

int32_t amipr_atomic_cas(volatile int32_t* p, int32_t cmp, int32_t xchg)
{
	int32_t old;
	Disable();
	old = *p;
	if (old == cmp)
		*p = xchg;
	Enable();
	return old;
}

int64_t amipr_atomic_cas64(volatile int64_t* p, int64_t cmp, int64_t xchg)
{
	int64_t old;
	Disable();
	old = *p;
	if (old == cmp)
		*p = xchg;
	Enable();
	return old;
}

/* compiler runtime entry points */
#define AMIPR_SYNC_FETCH_OP(name, op)                                          \
	unsigned int name(volatile void* vp, unsigned int v)                        \
	{                                                                          \
		volatile unsigned int* p = vp;                                          \
		unsigned int old;                                                      \
		Disable();                                                             \
		old = *p;                                                              \
		*p = old op v;                                                         \
		Enable();                                                              \
		return old;                                                            \
	}
#define AMIPR_SYNC_OP_FETCH(name, op)                                          \
	unsigned int name(volatile void* vp, unsigned int v)                        \
	{                                                                          \
		volatile unsigned int* p = vp;                                          \
		unsigned int r;                                                        \
		Disable();                                                             \
		r = *p op v;                                                           \
		*p = r;                                                                \
		Enable();                                                              \
		return r;                                                              \
	}

AMIPR_SYNC_FETCH_OP(__sync_fetch_and_add_4, +)
AMIPR_SYNC_FETCH_OP(__sync_fetch_and_sub_4, -)
AMIPR_SYNC_FETCH_OP(__sync_fetch_and_or_4, |)
AMIPR_SYNC_FETCH_OP(__sync_fetch_and_and_4, &)
AMIPR_SYNC_FETCH_OP(__sync_fetch_and_xor_4, ^)
AMIPR_SYNC_OP_FETCH(__sync_add_and_fetch_4, +)
AMIPR_SYNC_OP_FETCH(__sync_sub_and_fetch_4, -)
AMIPR_SYNC_OP_FETCH(__sync_or_and_fetch_4, |)
AMIPR_SYNC_OP_FETCH(__sync_and_and_fetch_4, &)
AMIPR_SYNC_OP_FETCH(__sync_xor_and_fetch_4, ^)

unsigned int __sync_val_compare_and_swap_4(volatile void* vp, unsigned int cmp, unsigned int xchg)
{
	return (unsigned int)amipr_atomic_cas((volatile int32_t*)vp, (int32_t)cmp, (int32_t)xchg);
}

_Bool __sync_bool_compare_and_swap_4(volatile void* vp, unsigned int cmp, unsigned int xchg)
{
	return __sync_val_compare_and_swap_4(vp, cmp, xchg) == cmp;
}

unsigned int __sync_lock_test_and_set_4(volatile void* vp, unsigned int v)
{
	volatile unsigned int* p = vp;
	unsigned int old;
	Disable();
	old = *p;
	*p = v;
	Enable();
	return old;
}

unsigned long long __sync_val_compare_and_swap_8(volatile void* vp, unsigned long long cmp,
                                                 unsigned long long xchg)
{
	return (unsigned long long)amipr_atomic_cas64((volatile int64_t*)vp, (int64_t)cmp,
	                                              (int64_t)xchg);
}

void __sync_synchronize(void)
{
	/* one CPU, in-order memory as seen by other tasks: nothing to do */
}
