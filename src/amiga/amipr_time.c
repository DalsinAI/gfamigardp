/*
 * amipr: clocks and sleeping (timer.device, locale.library).
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 *
 * The Amiga system clock keeps local time; locale.library's loc_GMTOffset
 * is the number of minutes the locale is west of GMT. The monotonic clock
 * is the E-clock (ReadEClock), which counts from boot and never goes back.
 */
#include "amipr_internal.h"

#include <libraries/locale.h>
#include <proto/locale.h>

uint64_t amipr_monotonic_us(void)
{
	struct EClockVal ev;
	uint64_t ticks;

	if (amipr_init() != AMIPR_OK)
		return 0;
	ReadEClock(&ev);
	ticks = ((uint64_t)ev.ev_hi << 32) | ev.ev_lo;
	/* split so the multiply cannot overflow for any realistic uptime */
	return (ticks / amipr_eclock_hz) * 1000000ULL +
	       ((ticks % amipr_eclock_hz) * 1000000ULL) / amipr_eclock_hz;
}

static LONG locale_gmt_offset(void)
{
	struct Library* LocaleBase = amipr_LocaleBase;
	struct Locale* loc;
	LONG off = 0;

	if (!LocaleBase)
		return 0;
	loc = OpenLocale(NULL);
	if (loc)
	{
		off = loc->loc_GMTOffset;
		CloseLocale(loc);
	}
	return off;
}

int amipr_utc_offset_minutes(void)
{
	if (amipr_init() != AMIPR_OK)
		return 0;
	return (int)-locale_gmt_offset();
}

const char* amipr_timezone_name(void)
{
	static char name[8];
	struct Library* LocaleBase;
	struct Locale* loc;
	ULONG code;

	if (amipr_init() != AMIPR_OK || !amipr_LocaleBase)
		return "";
	LocaleBase = amipr_LocaleBase;
	loc = OpenLocale(NULL);
	if (!loc)
		return "";
	code = loc->loc_CountryCode;
	CloseLocale(loc);
	name[0] = (char)(code >> 24);
	name[1] = (char)(code >> 16);
	name[2] = (char)(code >> 8);
	name[3] = (char)code;
	name[4] = '\0';
	return name;
}

int64_t amipr_unix_time_us(void)
{
	struct timeval tv;

	if (amipr_init() != AMIPR_OK)
		return 0;
	GetSysTime(&tv);
	return ((int64_t)tv.tv_secs + AMIPR_EPOCH_DIFF + (int64_t)locale_gmt_offset() * 60) * 1000000LL +
	       tv.tv_micro;
}

void amipr_yield(void)
{
	/* Let tasks of the same priority run: a zero-length timer request. */
	amipr_sleep_us(0);
}

void amipr_sleep_us(uint64_t us)
{
	struct amipr_task_res* r = amipr_task_res();

	if (!r)
	{
		/* no timer: fall back to dos.library ticks (50 per second) */
		Delay((ULONG)((us + 19999) / 20000));
		return;
	}
	r->tr->tr_node.io_Command = TR_ADDREQUEST;
	r->tr->tr_time.tv_secs = (ULONG)(us / 1000000ULL);
	r->tr->tr_time.tv_micro = (ULONG)(us % 1000000ULL);
	DoIO((struct IORequest*)r->tr);
}
