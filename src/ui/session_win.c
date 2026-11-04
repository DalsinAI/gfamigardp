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
	int result; /* -1 running, 0 quit, 1 back to the connection window */
};

static void status(struct sw* s, const char* text)
{
	olui_set_text(s->sb, olui_find(s->sb, "status"), text);
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

int session_window(struct app* app, const struct session_info* info)
{
	struct sw s;
	struct Menu* menu;
	UWORD fh = app->screen->Font ? app->screen->Font->ta_YSize : 8;
	char title[300], line[400];
	int ok;

	memset(&s, 0, sizeof(s));
	s.info = info;
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
		snprintf(line, sizeof(line), "%u \xC3\x97 %u, %u-bit \xC2\xB7 %s", (unsigned)info->width,
		         (unsigned)info->height, (unsigned)info->depth, info->security);
		olui_set_text(s.sb, olui_find(s.sb, "mode"), line);
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
		while (s.result < 0)
		{
			struct IntuiMessage* m;
			ULONG sig = Wait((1UL << s.win->UserPort->mp_SigBit) | SIGBREAKF_CTRL_C);
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
					draw_desktop(&s);
			}
			olui_amiga_draw(s.atb);
			olui_amiga_draw(s.asb);
		}
	}
	else
		s.result = 1;
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
