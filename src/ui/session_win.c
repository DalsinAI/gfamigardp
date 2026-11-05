/*
 * GFAmigaRDP: the session window: a toolbar strip at the top and a status
 * bar at the foot, both OpenLayout UI (olui_amiga_strip, gfopenlayout
 * library 11), and the remote desktop between them, which the program
 * draws itself (desktop.c). The keys belong to the remote computer while
 * the desktop has them (OLUI_AMIGA_KEYS_FOCUS); the program's own are the
 * Amiga-key menus.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "ui.h"
#include "session.h"
#include "amipr.h"

#include <exec/types.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <stdio.h>
#include <string.h>

#include "gfopenlayout_ui.h"
#include "gfopenlayout_ui_amiga.h"


enum { M_DISC = 1, M_CAD, M_QUIT, M_FULL, M_ABOUT };

static const struct NewMenu menus[] = {
	{ NM_TITLE, (STRPTR) "Project", NULL, 0, 0, NULL },
	{ NM_ITEM, (STRPTR) "Disconnect", (STRPTR) "D", 0, 0, (APTR)M_DISC },
	{ NM_ITEM, (STRPTR) "Send Ctrl-Alt-Del", NULL, 0, 0, (APTR)M_CAD },
	{ NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
	{ NM_ITEM, (STRPTR) "Quit", (STRPTR) "Q", 0, 0, (APTR)M_QUIT },
	{ NM_TITLE, (STRPTR) "View", NULL, 0, 0, NULL },
	{ NM_ITEM, (STRPTR) "Full screen", (STRPTR) "F", 0, 0, (APTR)M_FULL },
	{ NM_TITLE, (STRPTR) "Help", NULL, 0, 0, NULL },
	{ NM_ITEM, (STRPTR) "About...", NULL, 0, 0, (APTR)M_ABOUT },
	{ NM_END, NULL, NULL, 0, 0, NULL },
};

struct sw
{
	struct Window* win;
	olui *tb, *sb;
	olui_amiga *atb, *asb;
	int tb_h, sb_h;
	const struct session_info* info;
	struct rdp_conn* conn;   /* live: the session's connection, closed when the window closes */
	int result; /* -1 running, 0 quit, 1 back to the connection window */
	int shown_act;           /* what the status bar last said, so it changes only when the session does */
	uint32_t shown_pdus;
};

static void status(struct sw* s, const char* text)
{
	olui_set_text(s->sb, olui_find(s->sb, "status"), text);
}

/* the mode line: the size the computer gave in its Demand Active once it has */
static void show_mode(struct sw* s)
{
	char line[400];
	const struct session_info* info = s->info;
	unsigned w = info->width, h = info->height, d = info->depth;
	if (s->conn && s->conn->act >= RDP_ACT_DEMAND && s->conn->demand.width)
	{
		w = s->conn->demand.width;
		h = s->conn->demand.height;
		if (s->conn->demand.bpp)
			d = s->conn->demand.bpp;
	}
	/* plain ASCII in the strips (review M9) */
	snprintf(line, sizeof(line), "%u x %u, %u-bit - %s", w, h, d, info->security);
	olui_set_text(s->sb, olui_find(s->sb, "mode"), line);
}

/* the session's state in words, on the status line */
static void show_session(struct sw* s, int rc)
{
	struct rdp_conn* c = s->conn;
	char line[300];
	const char* ei;
	if (!c)
		return;
	if (c->act == s->shown_act && rc >= 0 && (c->pdus - s->shown_pdus) < 50)
		return;
	s->shown_act = c->act;
	s->shown_pdus = c->pdus;
	ei = rdp_error_info_text(c->error_info);
	if (c->act == RDP_ACT_ENDED || (rc < 0 && rc != AMIPR_ETIMEOUT))
	{
		if (c->error_info)
			snprintf(line, sizeof(line), "The computer ended the session: %s (0x%08lX).",
			         ei ? ei : "error", (unsigned long)c->error_info);
		else if (c->deactivated_early)
			snprintf(line, sizeof(line), "The computer ended the session after licensing, most likely as it needs the "
			         "Graphics Pipeline (RDPGFX): GFAmigaRDP has not got it yet.");
		else if (c->mr.disconnect_reason)
			snprintf(line, sizeof(line), "The computer ended the session (reason %u).", c->mr.disconnect_reason - 1);
		else
			snprintf(line, sizeof(line), "The session stopped (%s): %s", rdp_act_name(c->act), rdp_error_text(rc));
	}
	else if (c->act == RDP_ACT_ACTIVE)
		snprintf(line, sizeof(line), "Session active; %lu PDUs (%lu fast-path updates). The desktop is not drawn yet.",
		         (unsigned long)c->pdus, (unsigned long)c->fp.updates);
	else
		snprintf(line, sizeof(line), "Starting the session: %s.", rdp_act_name(c->act));
	status(s, line);
	show_mode(s);
}

/* reads what the computer sent, without waiting; ends the connection when it ended */
static void poll_session(struct sw* s)
{
	int rc;
	if (!s->conn || !s->conn->live)
		return;
	rc = rdp_conn_poll(s->conn, 16);
	show_session(s, rc);
	if (rc < 0)
		rdp_conn_close(s->conn);
}

static void action(struct sw* s, int what)
{
	switch (what)
	{
		case M_DISC: s->result = 1; break;
		case M_QUIT: s->result = 0; break;
		case M_CAD:
			status(s, s->info->demo ? "Demo: Ctrl-Alt-Del would go to the computer now." :
			                          "Ctrl-Alt-Del goes to the computer once input is written.");
			break;
		case M_FULL: status(s, "Full screen comes with the session's own screen (roadmap phase 4)."); break;
		case M_ABOUT: ui_about(s->win); break;
	}
}

static void on_tb(olui* ui, const olui_event* ev, void* data)
{
	struct sw* s = data;
	(void)ui;
	if (!strcmp(ev->id, "disc")) action(s, M_DISC);
	else if (!strcmp(ev->id, "cad")) action(s, M_CAD);
	else if (!strcmp(ev->id, "full")) action(s, M_FULL);
	else if (!strcmp(ev->id, "keys")) status(s, "Keys: every key but the Amiga keys goes to the computer.");
}

static void draw_desktop(struct sw* s)
{
	struct Window* w = s->win;
	int x = w->BorderLeft, y = w->BorderTop + s->tb_h;
	int iw = w->Width - w->BorderLeft - w->BorderRight;
	int ih = w->Height - w->BorderTop - w->BorderBottom - s->tb_h - s->sb_h;
	desktop_draw(w, x, y, iw, ih, s->info);
}

static void menu_pick(struct sw* s, UWORD code)
{
	while (code != MENUNULL && s->result < 0)
	{
		struct MenuItem* item = ItemAddress(s->win->MenuStrip, code);
		if (!item)
			break;
		action(s, (int)(ULONG)GTMENUITEM_USERDATA(item));
		code = item->NextSelect;
	}
}

static int setup(struct sw* s, olui** ui, olui_amiga** a, const char* markup, const char* css)
{
	char err[128];
	if (!(*ui = olui_new()) || !(*a = olui_amiga_new(*ui, s->win)))
		return 0;
	olui_amiga_theme(*a, NULL);
	if (!olui_load(*ui, markup, css, err, sizeof(err)))
	{
		Printf((CONST_STRPTR)APP_NAME ": %s\n", (ULONG)err);
		return 0;
	}
	olui_amiga_keys(*a, OLUI_AMIGA_KEYS_FOCUS);
	return 1;
}

/* the whole Capabilities Exchange and finalization, from the end of
 * licensing to the Font Map */
#define ACTIVATE_US 60000000ULL

int session_window(struct app* app, const struct session_info* info)
{
	struct sw s;
	struct Menu* menu;
	UWORD fh = app->screen->Font ? app->screen->Font->ta_YSize : 8;
	char title[320], line[520];
	int ok;

	memset(&s, 0, sizeof(s));
	s.info = info;
	s.conn = (!info->demo && info->conn && info->conn->live) ? info->conn : NULL;
	s.shown_act = -1;
	s.result = -1;
	s.tb_h = fh + 12;
	s.sb_h = fh + 6;
	snprintf(title, sizeof(title), APP_NAME " \xB7 %s%s", info->host, info->demo ? " (demo)" : "");

	s.win = OpenWindowTags(NULL,
		WA_Title, (ULONG)title,
		WA_ScreenTitle, (ULONG)APP_NAME " " APP_VERSION,
		WA_PubScreen, (ULONG)app->screen,
		WA_InnerWidth, info->width, WA_InnerHeight, info->height + s.tb_h + s.sb_h,
		WA_AutoAdjust, TRUE,
		WA_MinWidth, 320, WA_MinHeight, 120 + s.tb_h + s.sb_h, WA_MaxWidth, ~0UL, WA_MaxHeight, ~0UL,
		WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
		WA_SizeBBottom, TRUE,
		WA_Activate, TRUE, WA_SimpleRefresh, TRUE, WA_ReportMouse, TRUE, WA_NewLookMenus, TRUE,
		WA_IDCMP, olui_amiga_idcmp() | IDCMP_MENUPICK, TAG_DONE);
	if (!s.win)
		return 1;
	menu = ui_menus(app, menus);
	if (menu)
		SetMenuStrip(s.win, menu);

	ok = setup(&s, &s.tb, &s.atb, ui_toolbar_markup, ui_toolbar_css) && setup(&s, &s.sb, &s.asb, ui_status_markup, ui_status_css);
	if (ok)
	{
		olui_on(s.tb, NULL, OLUI_CLICK, on_tb, &s);
		snprintf(line, sizeof(line), "%s%s%s", info->user, info->user[0] ? " on " : "", info->host);
		olui_set_text(s.tb, olui_find(s.tb, "where"), line);
		show_mode(&s);
		status(&s, info->demo ? "Demo session: no server. Amiga-D disconnects." :
		                        "Connected and secured. The desktop itself is not drawn yet.");
		/* both strips need library 11; the static libolui always has them */
		/* the strips first: olui_amiga_layout draws all it has, the whole window
		 * when it has no strip yet (and the status bar's row went to the top) */
		ok = olui_amiga_strip(s.atb, 0, 0, 0, s.tb_h) && olui_amiga_strip(s.asb, 0, -s.sb_h, 0, 0) &&
		     olui_amiga_layout(s.atb) && olui_amiga_layout(s.asb);
	}
	if (ok)
	{
		draw_desktop(&s);
		/* the Capabilities Exchange and the finalization on the connection
		 * the connection window opened (review B1) go on in the loop below,
		 * PDU by PDU as they come, so the window answers all the while
		 * (review M2); they get ACTIVATE_US in all */
		uint64_t act_deadline = 0;
		if (s.conn)
		{
			status(&s, "Starting the session: waiting for Demand Active.");
			olui_amiga_draw(s.asb);
			act_deadline = amipr_monotonic_us() + ACTIVATE_US;
		}
		while (s.result < 0)
		{
			struct IntuiMessage* m;
			ULONG winsig = 1UL << s.win->UserPort->mp_SigBit;
			ULONG sig;
			if (s.conn && s.conn->live)
			{
				/* the socket and the window together: what the computer
				 * sends is read when it comes, whichever window is active
				 * (review M3); timeout 0 when TLS holds bytes already or a
				 * whole PDU waits (review M4) */
				struct amipr_wait_item it;
				uint32_t sigs = winsig | SIGBREAKF_CTRL_C;
				int64_t timeout = -1;
				int rc;
				if (rdp_conn_ready(s.conn))
					timeout = 0;
				else if (s.conn->act != RDP_ACT_ACTIVE)
					timeout = 1000000;   /* to see the activation deadline */
				it.fd = s.conn->t.sock;
				it.events = AMIPR_WAIT_READ;
				it.revents = 0;
				rc = amipr_wait_signals(&it, 1, timeout, &sigs);
				sig = sigs;
				if (rc != 0 || timeout == 0)
					poll_session(&s);
				if (s.conn->live && s.conn->act != RDP_ACT_ACTIVE && s.conn->act != RDP_ACT_ENDED &&
				    amipr_monotonic_us() >= act_deadline)
				{
					rdp_conn_close(s.conn);
					status(&s, "The computer did not finish starting the session in time.");
				}
			}
			else
				sig = Wait(winsig | SIGBREAKF_CTRL_C);
			if (sig & SIGBREAKF_CTRL_C)
				s.result = 0;
			while (s.result < 0 && (m = (struct IntuiMessage*)GetMsg(s.win->UserPort)))
			{
				ULONG cls = m->Class;
				UWORD code = m->Code;
				int r1, r2;
				if (cls == IDCMP_MENUPICK)
				{
					ReplyMsg(&m->ExecMessage);
					menu_pick(&s, code);
					continue;
				}
				/* the strips first (gfopenlayout's strip contract): on NEWSIZE
				 * each lays itself out again for the new width, on
				 * REFRESHWINDOW each draws itself; then the desktop between
				 * them, inside the program's BeginRefresh/EndRefresh so the
				 * damage list is cleared only after everything is back
				 * (review H3, M5) */
				r1 = olui_amiga_event(s.atb, m);
				r2 = olui_amiga_event(s.asb, m);
				ReplyMsg(&m->ExecMessage);
				if (r1 == OLUI_AMIGA_CLOSE || r2 == OLUI_AMIGA_CLOSE || cls == IDCMP_CLOSEWINDOW)
					s.result = 1;
				else if (cls == IDCMP_REFRESHWINDOW)
				{
					BeginRefresh(s.win);
					draw_desktop(&s);
					EndRefresh(s.win, TRUE);
				}
				else if (cls == IDCMP_NEWSIZE)
				{
					/* a new size: the strips laid out again (above), the
					 * desktop band between them is now somewhere else */
					olui_amiga_draw(s.atb);
					olui_amiga_draw(s.asb);
					draw_desktop(&s);
				}
			}
			olui_amiga_draw(s.atb);
			olui_amiga_draw(s.asb);
		}
	}
	else
		s.result = 1;
	if (s.conn)
		rdp_conn_close(s.conn);   /* the Disconnect Provider Ultimatum when it is still up */
	olui_amiga_free(s.atb);
	olui_free(s.tb);
	olui_amiga_free(s.asb);
	olui_free(s.sb);
	if (menu)
	{
		ClearMenuStrip(s.win);
		FreeMenus(menu);
	}
	CloseWindow(s.win);
	return s.result;
}
