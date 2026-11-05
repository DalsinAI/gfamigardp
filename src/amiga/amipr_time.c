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
#include <devices/keymap.h>
#include <proto/keymap.h>
#include <string.h>

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

uint32_t amipr_keyboard_layout(void)
{
	/* Windows KLID from the locale country (ISO 3166-1 alpha-3 as Amiga
	 * packs it in loc_CountryCode). Common European and Americas layouts;
	 * unknown countries fall back to US (0x0409). */
	static const struct { uint32_t code; uint32_t klid; } map[] = {
		{ 0x55534100u /* USA */, 0x00000409 },
		{ 0x47425200u /* GBR */, 0x00000809 },
		{ 0x49524c00u /* IRL */, 0x00001809 },
		{ 0x41555300u /* AUS */, 0x00000c09 },
		{ 0x4e5a4c00u /* NZL */, 0x00001409 },
		{ 0x43414e00u /* CAN */, 0x00001009 },
		{ 0x44455500u /* DEU */, 0x00000407 },
		{ 0x41555400u /* AUT */, 0x00000c07 },
		{ 0x43484500u /* CHE */, 0x00000807 }, /* Swiss German; FR-CH is 100c */
		{ 0x46524100u /* FRA */, 0x0000040c },
		{ 0x42454c00u /* BEL */, 0x0000080c },
		{ 0x49544100u /* ITA */, 0x00000410 },
		{ 0x45535000u /* ESP */, 0x0000040a },
		{ 0x4e4c4400u /* NLD */, 0x00000413 },
		{ 0x53574500u /* SWE */, 0x0000041d },
		{ 0x4e4f5200u /* NOR */, 0x00000414 },
		{ 0x444e4b00u /* DNK */, 0x00000406 },
		{ 0x46494e00u /* FIN */, 0x0000040b },
		{ 0x504f4c00u /* POL */, 0x00000415 },
		{ 0x435a4500u /* CZE */, 0x00000405 },
		{ 0x48554e00u /* HUN */, 0x0000040e },
		{ 0x50525400u /* PRT */, 0x00000816 },
		{ 0x42524100u /* BRA */, 0x00000416 },
		{ 0x52555300u /* RUS */, 0x00000419 },
		{ 0x54555200u /* TUR */, 0x0000041f },
		{ 0x4a504e00u /* JPN */, 0x00000411 },
	};
	struct Library* LocaleBase;
	struct Locale* loc;
	ULONG code;
	unsigned i;

	if (amipr_init() != AMIPR_OK || !amipr_LocaleBase)
		return 0x00000409;
	LocaleBase = amipr_LocaleBase;
	loc = OpenLocale(NULL);
	if (!loc)
		return 0x00000409;
	code = loc->loc_CountryCode;
	CloseLocale(loc);
	for (i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (map[i].code == code)
			return map[i].klid;
	return 0x00000409;
}

const char* amipr_keymap_name(void)
{
	/* keymap.library's default keymap is the kn_KeyMap of a KeyMapNode
	 * on keymap.resource's list; its node's name is the keymap's
	 * (review 2 N7). Looked for, not assumed: "" when it is not there. */
	static char name[32];
	struct Library* KeymapBase;
	struct KeyMapResource* kr;
	struct KeyMap* km;
	struct Node* n;

	name[0] = 0;
	KeymapBase = OpenLibrary((CONST_STRPTR) "keymap.library", 37);
	if (!KeymapBase)
		return name;
	km = AskKeyMapDefault();
	kr = (struct KeyMapResource*)OpenResource((CONST_STRPTR) "keymap.resource");
	if (km && kr)
	{
		Forbid();
		for (n = kr->kr_List.lh_Head; n->ln_Succ; n = n->ln_Succ)
			if (&((struct KeyMapNode*)n)->kn_KeyMap == km)
			{
				if (n->ln_Name)
					strncpy(name, (const char*)n->ln_Name, sizeof(name) - 1);
				break;
			}
		Permit();
	}
	CloseLibrary(KeymapBase);
	return name;
}
