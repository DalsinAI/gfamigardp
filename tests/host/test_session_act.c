/*
 * GFAmigaRDP host tests: the session window's activation deadline
 * (rdp_act_deadline_passed in session.h, review 2 N1): a Deactivate All
 * after the session was active gets a fresh span and does not end it.
 *   cc -I../../src/core -o test_session_act test_session_act.c
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 5).
 */
#include <stdio.h>
#include <stdint.h>
#include "session.h"

static int fails, passes;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define S 1000000ULL
#define SPAN (60 * S)   /* session_win.c's ACTIVATE_US */

int main(void)
{
	uint64_t dl = 0, t;
	int live, act;

	/* the window opens: armed now + 60 s */
	CHECK(!rdp_act_deadline_passed(RDP_ACT_NONE, 0, SPAN, &dl), "armed, not passed");
	CHECK(dl == SPAN, "deadline %llu", (unsigned long long)dl);
	CHECK(!rdp_act_deadline_passed(RDP_ACT_CONFIRMED, 30 * S, SPAN, &dl), "30 s in");
	CHECK(!rdp_act_deadline_passed(RDP_ACT_ACTIVE, 40 * S, SPAN, &dl), "active");
	CHECK(dl == 0, "cleared when active");
	CHECK(!rdp_act_deadline_passed(RDP_ACT_ACTIVE, 500 * S, SPAN, &dl), "active long after");

	/* Deactivate All ten minutes in (a resize, a reconnection): the loop
	 * as session_win.c runs it, once a second; the server answers with a
	 * new Demand Active and the finalization within 5 s */
	live = 1;
	for (t = 600 * S; t <= 700 * S && live; t += S)
	{
		act = t < 602 * S ? RDP_ACT_DEACTIVATED : t < 605 * S ? RDP_ACT_DEMAND : RDP_ACT_ACTIVE;
		if (rdp_act_deadline_passed(act, t, SPAN, &dl))
			live = 0;
	}
	CHECK(live, "Deactivate All after ACTIVE ended the session");
	CHECK(dl == 0, "active again: cleared");

	/* a reactivation that never finishes still ends after the span */
	CHECK(!rdp_act_deadline_passed(RDP_ACT_DEACTIVATED, 1000 * S, SPAN, &dl), "re-armed");
	CHECK(dl == 1000 * S + SPAN, "re-armed at now + span");
	CHECK(!rdp_act_deadline_passed(RDP_ACT_DEMAND, 1059 * S, SPAN, &dl), "59 s");
	CHECK(rdp_act_deadline_passed(RDP_ACT_DEMAND, 1060 * S, SPAN, &dl), "60 s: passed");

	/* never active at all: the first deadline holds */
	dl = 0;
	CHECK(!rdp_act_deadline_passed(RDP_ACT_NONE, 5 * S, SPAN, &dl), "start");
	CHECK(rdp_act_deadline_passed(RDP_ACT_SYNC, 65 * S, SPAN, &dl), "start: passed at 60 s");

	/* ended by the server: no deadline */
	CHECK(!rdp_act_deadline_passed(RDP_ACT_ENDED, 9999 * S, SPAN, &dl), "ended");

	printf("session_act: %d passed, %d failed\n", passes, fails);
	return fails ? 1 : 0;
}
