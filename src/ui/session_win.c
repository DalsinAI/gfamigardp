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
#include "rdpgfx.h"
#include "input.h"

#include <exec/types.h>
#include <intuition/intuition.h>
#include <devices/inputevent.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <stdio.h>
#include <string.h>

#include "gfopenlayout_ui.h"
#include "gfopenlayout_ui_amiga.h"


enum { M_DISC = 1, M_CAD, M_QUIT, M_FULL, M_ABOUT, M_HOME, M_PAN_L, M_PAN_R, M_PAN_U, M_PAN_D };

static const struct NewMenu menus[] = {
	{ NM_TITLE, (STRPTR) "Project", NULL, 0, 0, NULL },
	{ NM_ITEM, (STRPTR) "Disconnect", (STRPTR) "D", 0, 0, (APTR)M_DISC },
	{ NM_ITEM, (STRPTR) "Send Ctrl-Alt-Del", NULL, 0, 0, (APTR)M_CAD },
	{ NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
	{ NM_ITEM, (STRPTR) "Quit", (STRPTR) "Q", 0, 0, (APTR)M_QUIT },
	{ NM_TITLE, (STRPTR) "View", NULL, 0, 0, NULL },
	{ NM_ITEM, (STRPTR) "Full screen", (STRPTR) "F", 0, 0, (APTR)M_FULL },
	{ NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
	{ NM_ITEM, (STRPTR) "Home (top-left)", (STRPTR) "H", 0, 0, (APTR)M_HOME },
	{ NM_ITEM, (STRPTR) "Pan left", NULL, 0, 0, (APTR)M_PAN_L },
	{ NM_ITEM, (STRPTR) "Pan right", NULL, 0, 0, (APTR)M_PAN_R },
	{ NM_ITEM, (STRPTR) "Pan up", NULL, 0, 0, (APTR)M_PAN_U },
	{ NM_ITEM, (STRPTR) "Pan down", NULL, 0, 0, (APTR)M_PAN_D },
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
	uint32_t shown_frames;
	uint64_t last_blit;      /* amipr_monotonic_us of the last blit: at most one a frame */
	uint8_t fb_shown;        /* 1: the desktop band shows the Graphics Output Buffer, not the pattern */
	int view_ox, view_oy;    /* pan: top-left of the band in remote/fb coordinates */
	int panning;             /* 1 while left-dragging the desktop to pan */
	int pan_mx, pan_my;      /* last mouse position during a pan drag */
};

/* 1/30 s: dirty rectangles that come faster wait for the next one */
#define BLIT_US 33000

static void band(struct sw* s, int* x, int* y, int* iw, int* ih);
static void draw_desktop(struct sw* s);
static void show_mode(struct sw* s);
static void pan_home(struct sw* s);
static void pan_by(struct sw* s, int dx, int dy);

static const char* codec_name(uint16_t id)
{
	switch (id)
	{
		case RDPGFX_CODECID_UNCOMPRESSED: return "Uncompressed";
		case RDPGFX_CODECID_CAVIDEO: return "RemoteFX";
		case RDPGFX_CODECID_CLEARCODEC: return "ClearCodec";
		case RDPGFX_CODECID_CAPROGRESSIVE: return "Progressive";
		case RDPGFX_CODECID_PLANAR: return "Planar";
		case RDPGFX_CODECID_ALPHA: return "Alpha";
		default: return "another codec";
	}
}

/* the session's Graphics Output Buffer once the server has drawn into it */
static struct rdp_fb* session_fb(struct sw* s)
{
	if (!s->conn || !s->conn->gfx || !s->conn->gfx->fb.px)
		return NULL;
	if (!s->fb_shown && !s->conn->gfx->fb.frames && !s->conn->gfx->fb.dirty.n)
		return NULL;
	return &s->conn->gfx->fb;
}

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
	/* plain ASCII in the strips (review M9); pan offset when not at the origin */
	{
		int x, y, iw, ih;
		band(s, &x, &y, &iw, &ih);
		(void)x; (void)y;
		if (s->view_ox || s->view_oy || (int)w > iw || (int)h > ih)
			snprintf(line, sizeof(line), "%d x %d of %u x %u +%d,+%d, %u-bit - %s",
			         iw < (int)w ? iw : (int)w, ih < (int)h ? ih : (int)h,
			         w, h, s->view_ox, s->view_oy, d, info->security);
		else
			snprintf(line, sizeof(line), "%u x %u, %u-bit - %s", w, h, d, info->security);
	}
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
	if (c->act == s->shown_act && rc >= 0 && (c->pdus - s->shown_pdus) < 50 &&
	    (!c->gfx || c->gfx->st.frames == s->shown_frames || (c->gfx->st.frames - s->shown_frames) < 30))
		return;
	s->shown_act = c->act;
	s->shown_pdus = c->pdus;
	s->shown_frames = c->gfx ? c->gfx->st.frames : 0;
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
	else if (c->act == RDP_ACT_ACTIVE && c->gfx && c->gfx->st.skipped_codec)
		snprintf(line, sizeof(line), "Session active; %lu frames. Parts not drawn: the computer uses %s "
		         "(%lu tiles), which GFAmigaRDP does not decode yet.",
		         (unsigned long)c->gfx->st.frames, codec_name(c->gfx->st.last_skipped_codec),
		         (unsigned long)c->gfx->st.skipped_codec);
	else if (c->act == RDP_ACT_ACTIVE && c->gfx)
		snprintf(line, sizeof(line), "Session active; %lu frames (Graphics Pipeline %s).",
		         (unsigned long)c->gfx->st.frames, c->gfx->caps_ok ? "on" : "starting");
	else if (c->act == RDP_ACT_ACTIVE)
		snprintf(line, sizeof(line), "Session active; %lu PDUs (%lu fast-path updates). The desktop is not drawn yet.",
		         (unsigned long)c->pdus, (unsigned long)c->fp.updates);
	else
		snprintf(line, sizeof(line), "Starting the session: %s.", rdp_act_name(c->act));
	status(s, line);
	show_mode(s);
}


/* Remote desktop coords from a window mouse position (pan offset). */
static void remote_xy(struct sw* s, int mx, int my, uint16_t* rx, uint16_t* ry)
{
	int bx, by, iw, ih, x, y;
	band(s, &bx, &by, &iw, &ih);
	x = mx - bx + s->view_ox;
	y = my - by + s->view_oy;
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x > 65535) x = 65535;
	if (y > 65535) y = 65535;
	*rx = (uint16_t)x;
	*ry = (uint16_t)y;
}

static int in_desktop_band(struct sw* s, int mx, int my)
{
	int bx, by, iw, ih;
	band(s, &bx, &by, &iw, &ih);
	return mx >= bx && mx < bx + iw && my >= by && my < by + ih;
}

/* Send mouse to the server when the session is live and not panning. */
static void send_mouse(struct sw* s, uint16_t flags, int mx, int my)
{
	uint16_t rx, ry;
	if (!s->conn || !s->conn->live || s->conn->act != RDP_ACT_ACTIVE || s->info->demo)
		return;
	remote_xy(s, mx, my, &rx, &ry);
	(void)rdp_conn_input_mouse(s->conn, flags, rx, ry);
}

static void send_rawkey(struct sw* s, UWORD code)
{
	uint8_t scan;
	int ext, release;
	if (!s->conn || !s->conn->live || s->conn->act != RDP_ACT_ACTIVE || s->info->demo)
		return;
	release = (code & IECODE_UP_PREFIX) != 0;
	code = (UWORD)(code & ~IECODE_UP_PREFIX);
	/* Wheel as mouse wheel events (olui uses 0x7A/0x7B). */
	if (code == 0x7A || code == 0x7B)
	{
		uint16_t flags = RDP_PTR_WHEEL | 0x0078; /* one notch */
		if (code == 0x7B)
			flags |= RDP_PTR_WHEEL_NEG;
		(void)rdp_conn_input_mouse(s->conn, flags, 0, 0);
		return;
	}
	if (!rdp_rawkey_to_scan(code, &scan, &ext))
		return;   /* LAmiga/RAmiga and unmapped keys stay local */
	(void)rdp_conn_input_key(s->conn, scan, ext, release);
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
			if (s->info->demo)
				status(s, "Demo: Ctrl-Alt-Del would go to the computer now.");
			else if (s->conn && s->conn->live && s->conn->act == RDP_ACT_ACTIVE)
			{
				(void)rdp_conn_input_cad(s->conn);
				status(s, "Ctrl-Alt-Del sent to the computer.");
			}
			else
				status(s, "Ctrl-Alt-Del waits until the session is active.");
			break;
		case M_FULL: status(s, "Full screen comes with the session's own screen (roadmap phase 4)."); break;
		case M_ABOUT: ui_about(s->win); break;
		case M_HOME: pan_home(s); break;
		case M_PAN_L: case M_PAN_R: case M_PAN_U: case M_PAN_D:
		{
			int x, y, iw, ih, step;
			band(s, &x, &y, &iw, &ih);
			(void)x; (void)y;
			step = iw > 64 ? iw / 2 : 32;
			if (what == M_PAN_U || what == M_PAN_D)
				step = ih > 64 ? ih / 2 : 32;
			pan_by(s,
			       what == M_PAN_L ? -step : what == M_PAN_R ? step : 0,
			       what == M_PAN_U ? -step : what == M_PAN_D ? step : 0);
			break;
		}
	}
}

static void on_tb(olui* ui, const olui_event* ev, void* data)
{
	struct sw* s = data;
	(void)ui;
	if (!strcmp(ev->id, "disc")) action(s, M_DISC);
	else if (!strcmp(ev->id, "cad")) action(s, M_CAD);
	else if (!strcmp(ev->id, "full")) action(s, M_FULL);
	else if (!strcmp(ev->id, "keys")) status(s, "Keys go to the computer. Alt/Ctrl-drag or middle-drag pans; Amiga menus stay local.");
	else if (!strcmp(ev->id, "home")) pan_home(s);
}

static void band(struct sw* s, int* x, int* y, int* iw, int* ih)
{
	struct Window* w = s->win;
	*x = w->BorderLeft;
	*y = w->BorderTop + s->tb_h;
	*iw = w->Width - w->BorderLeft - w->BorderRight;
	*ih = w->Height - w->BorderTop - w->BorderBottom - s->tb_h - s->sb_h;
}

static void clamp_pan(struct sw* s)
{
	int x, y, iw, ih, fw, fh;
	struct rdp_fb* fb = session_fb(s);
	band(s, &x, &y, &iw, &ih);
	(void)x; (void)y;
	fw = fb ? fb->w : (int)s->info->width;
	fh = fb ? fb->h : (int)s->info->height;
	if (fw <= iw) s->view_ox = 0;
	else if (s->view_ox > fw - iw) s->view_ox = fw - iw;
	if (fh <= ih) s->view_oy = 0;
	else if (s->view_oy > fh - ih) s->view_oy = fh - ih;
	if (s->view_ox < 0) s->view_ox = 0;
	if (s->view_oy < 0) s->view_oy = 0;
}

/* Tell the Graphics Pipeline which band is on screen so Progressive can
 * skip idwt/YCbCr for off-screen tiles, then paint them when we pan. */
static void sync_view(struct sw* s)
{
	int x, y, iw, ih;
	if (!s->conn || !s->conn->gfx)
		return;
	clamp_pan(s);
	band(s, &x, &y, &iw, &ih);
	(void)x; (void)y;
	if (iw < 0) iw = 0;
	if (ih < 0) ih = 0;
	rdp_gfx_set_view(s->conn->gfx, (uint16_t)s->view_ox, (uint16_t)s->view_oy,
	                 (uint16_t)(s->view_ox + iw),
	                 (uint16_t)(s->view_oy + ih));
}

static void pan_by(struct sw* s, int dx, int dy)
{
	s->view_ox += dx;
	s->view_oy += dy;
	clamp_pan(s);
	sync_view(s);
	if (s->conn && s->conn->gfx)
		rdp_gfx_paint_view(s->conn->gfx);
	draw_desktop(s);
	show_mode(s);
}

static void pan_home(struct sw* s)
{
	s->view_ox = s->view_oy = 0;
	clamp_pan(s);
	sync_view(s);
	if (s->conn && s->conn->gfx)
		rdp_gfx_paint_view(s->conn->gfx);
	draw_desktop(s);
	show_mode(s);
}

/* all of the band: the remote desktop once it has drawn, the test pattern before */
static void draw_desktop(struct sw* s)
{
	int x, y, iw, ih;
	struct rdp_fb* fb = session_fb(s);
	band(s, &x, &y, &iw, &ih);
	clamp_pan(s);
	band(s, &x, &y, &iw, &ih);
	if (fb)
	{
		struct rdp_dirty d;
		rdp_fb_take_dirty(fb, &d, NULL);   /* all of it is drawn now */
		desktop_draw_fb(s->win, x, y, iw, ih, fb, s->view_ox, s->view_oy);
		s->fb_shown = 1;
		s->last_blit = amipr_monotonic_us();
	}
	else
		desktop_draw(s->win, x, y, iw, ih, s->info);
}

/* what changed since the last blit, at most once a frame (BLIT_US); 1 when
 * some is left for later */
static int blit_dirty(struct sw* s, int force)
{
	struct rdp_fb* fb = session_fb(s);
	struct rdp_dirty d;
	uint64_t now;
	int x, y, iw, ih;
	if (!fb || !fb->dirty.n)
		return 0;
	if (!s->fb_shown)
	{
		draw_desktop(s);   /* the first frame: the pattern goes */
		return 0;
	}
	now = amipr_monotonic_us();
	if (!force && now - s->last_blit < BLIT_US)
		return 1;
	if (rdp_fb_take_dirty(fb, &d, NULL) <= 0)
		return 0;
	clamp_pan(s);
	band(s, &x, &y, &iw, &ih);
	desktop_blit(s->win, x, y, iw, ih, fb, d.r, d.n, s->view_ox, s->view_oy);
	s->last_blit = now;
	return 0;
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
		WA_RMBTrap, TRUE,   /* right-click is remote MENUDOWN, not the Intuition menu */
		WA_IDCMP, olui_amiga_idcmp() | IDCMP_MENUPICK | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE, TAG_DONE);
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
		status(&s, info->demo ? "Demo: keys/mouse stay local. Alt/Ctrl or middle-drag pans; Amiga-H homes." :
		                        "Connected. Keys and mouse go to the computer; Alt/Ctrl or middle-drag pans.");
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
		int blit_pending = 0;
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
				if (timeout != 0 && blit_pending)
					timeout = BLIT_US;   /* the dirty rectangles held back */
				it.fd = s.conn->t.sock;
				it.events = AMIPR_WAIT_READ;
				if (rdp_conn_input_pending(s.conn))
					it.events |= AMIPR_WAIT_WRITE;
				it.revents = 0;
				sync_view(&s);   /* Progressive view-tile skip follows the pan */
				rc = amipr_wait_signals(&it, 1, timeout, &sigs);
				sig = sigs;
				if (rc != 0 || timeout == 0 || (it.revents & AMIPR_WAIT_WRITE))
					poll_session(&s);
				/* the desktop: what the frames changed, at most once a
				 * frame; while more waits to be read it can wait too */
				blit_pending = blit_dirty(&s, 0);
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
				UWORD qual = m->Qualifier;
				WORD mx = m->MouseX, my = m->MouseY;
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
					clamp_pan(&s);
					sync_view(&s);
					if (s.conn && s.conn->gfx)
						rdp_gfx_paint_view(s.conn->gfx);
					draw_desktop(&s);
					show_mode(&s);
				}
				else if (cls == IDCMP_MOUSEBUTTONS)
				{
					int in_band = in_desktop_band(&s, (int)mx, (int)my);
					int pan_btn;
					/* Pan: middle-drag, or Alt/Ctrl + left-drag. Plain left
					 * (and right) go to the remote desk. */
					pan_btn = (code == MIDDLEDOWN) ||
					          (code == SELECTDOWN &&
					           (qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT | IEQUALIFIER_CONTROL)));
					if (pan_btn && in_band)
					{
						s.panning = 1;
						s.pan_mx = (int)mx;
						s.pan_my = (int)my;
					}
					else if (code == SELECTUP || code == MIDDLEUP || code == MENUUP)
					{
						if (s.panning)
							s.panning = 0;
						else if (in_band && !s.info->demo)
						{
							uint16_t f = 0;
							if (code == SELECTUP) f = RDP_PTR_BUTTON1;
							else if (code == MENUUP) f = RDP_PTR_BUTTON2;
							else if (code == MIDDLEUP) f = RDP_PTR_BUTTON3;
							if (f)
								send_mouse(&s, f, (int)mx, (int)my); /* up: no DOWN bit */
						}
					}
					else if (in_band && !s.panning && !s.info->demo)
					{
						uint16_t f = RDP_PTR_DOWN;
						if (code == SELECTDOWN) f |= RDP_PTR_BUTTON1;
						else if (code == MENUDOWN) f |= RDP_PTR_BUTTON2;
						else if (code == MIDDLEDOWN) f |= RDP_PTR_BUTTON3;
						else f = 0;
						if (f)
							send_mouse(&s, f, (int)mx, (int)my);
					}
				}
				else if (cls == IDCMP_MOUSEMOVE)
				{
					if (s.panning)
					{
						int dx = s.pan_mx - (int)mx;
						int dy = s.pan_my - (int)my;
						s.pan_mx = (int)mx;
						s.pan_my = (int)my;
						if (dx || dy)
							pan_by(&s, dx, dy);
					}
					else if (in_desktop_band(&s, (int)mx, (int)my) && !s.info->demo)
						send_mouse(&s, RDP_PTR_MOVE, (int)mx, (int)my);
				}
				else if (cls == IDCMP_RAWKEY && r1 == OLUI_AMIGA_NONE && r2 == OLUI_AMIGA_NONE)
				{
					/* Keys: Amiga menus keep L/R Amiga; the rest go remote
					 * when no strip widget has focus (KEYS_FOCUS). */
					send_rawkey(&s, code);
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
