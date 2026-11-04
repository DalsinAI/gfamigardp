/*
 * GFAmigaRDP: the connection window (OpenLayout UI, from The Open SDK's
 * olui-app template). Open Apps rules: every control has a key (the
 * underlined letter when no field has the keyboard, Tab between them,
 * Return to connect, Esc to cancel, and the Amiga-key menus), plain
 * words, the theme's colours only, a status line at the foot.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "ui.h"
#include "amipr.h"
#include "connect.h"
#include "nla.h"
#include "mcs.h"
#include "x509.h"

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


enum
{
	M_CONNECT = 1, M_SAVE, M_DELETE, M_QUIT, M_CUT, M_COPY, M_PASTE, M_ABOUT
};

static const struct NewMenu menus[] = {
	{ NM_TITLE, (STRPTR) "Project", NULL, 0, 0, NULL },
	{ NM_ITEM, (STRPTR) "Connect", (STRPTR) "O", 0, 0, (APTR)M_CONNECT },
	{ NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
	{ NM_ITEM, (STRPTR) "Save as favourite", (STRPTR) "S", 0, 0, (APTR)M_SAVE },
	{ NM_ITEM, (STRPTR) "Delete favourite", NULL, 0, 0, (APTR)M_DELETE },
	{ NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
	{ NM_ITEM, (STRPTR) "Quit", (STRPTR) "Q", 0, 0, (APTR)M_QUIT },
	{ NM_TITLE, (STRPTR) "Edit", NULL, 0, 0, NULL },
	{ NM_ITEM, (STRPTR) "Cut", (STRPTR) "X", 0, 0, (APTR)M_CUT },
	{ NM_ITEM, (STRPTR) "Copy", (STRPTR) "C", 0, 0, (APTR)M_COPY },
	{ NM_ITEM, (STRPTR) "Paste", (STRPTR) "V", 0, 0, (APTR)M_PASTE },
	{ NM_TITLE, (STRPTR) "Help", NULL, 0, 0, NULL },
	{ NM_ITEM, (STRPTR) "About...", NULL, 0, 0, (APTR)M_ABOUT },
	{ NM_END, NULL, NULL, 0, 0, NULL },
};

struct cw
{
	struct app* app;
	struct Window* win;
	olui* ui;
	olui_amiga* a;
	olui_el *host, *user, *domain, *password, *res, *depth, *sec, *favs, *status;
	olui_el* fav_items[MAX_FAVOURITES];
	struct session_info* info;
	int result; /* -1 running, 0 quit, 1 connect */
};

/* Edit: the program's own clipboard for now (clipboard.device next) */
static char s_clip[512];

static void set_status(struct cw* c, const char* text)
{
	olui_set_text(c->ui, c->status, text);
}

static void fill_list(struct cw* c)
{
	int i;
	char v[8];

	olui_list_clear(c->ui, c->favs);
	for (i = 0; i < c->app->nfav; i++)
	{
		sprintf(v, "%d", i);
		c->fav_items[i] = olui_list_add(c->ui, c->favs, NULL, c->app->fav[i].name, v);
	}
}

static int selected_fav(struct cw* c)
{
	olui_el* it = olui_list_selected(c->favs);
	int i;

	for (i = 0; it && i < c->app->nfav; i++)
		if (c->fav_items[i] == it)
			return i;
	return -1;
}

static void show_fav(struct cw* c, int i)
{
	const struct favourite* f = &c->app->fav[i];

	olui_set_text(c->ui, c->host, f->host);
	olui_set_text(c->ui, c->user, f->user);
	olui_set_text(c->ui, c->domain, f->domain);
	olui_set_text(c->ui, c->password, "");
	olui_select_set(c->ui, c->res, f->res);
	olui_select_set(c->ui, c->depth, f->depth);
	olui_select_set(c->ui, c->sec, f->security);
}

static void copy_text(char* dst, size_t size, const char* src)
{
	strncpy(dst, src ? src : "", size - 1);
	dst[size - 1] = 0;
}

static void do_save(struct cw* c)
{
	const char* host = olui_get_text(c->host);
	struct favourite f;
	int i, slot = -1;
	char line[128];

	if (!host[0])
	{
		set_status(c, "Type the computer's name first.");
		olui_focus(c->ui, c->host);
		return;
	}
	memset(&f, 0, sizeof(f));
	copy_text(f.host, sizeof(f.host), host);
	copy_text(f.name, sizeof(f.name), host);
	copy_text(f.user, sizeof(f.user), olui_get_text(c->user));
	copy_text(f.domain, sizeof(f.domain), olui_get_text(c->domain));
	f.res = olui_select_index(c->res);
	f.depth = olui_select_index(c->depth);
	f.security = olui_select_index(c->sec);
	for (i = 0; i < c->app->nfav; i++)
		if (!strcmp(c->app->fav[i].host, f.host))
		{
			slot = i;
			copy_text(f.name, sizeof(f.name), c->app->fav[i].name);
		}
	if (slot < 0)
	{
		if (c->app->nfav >= MAX_FAVOURITES)
		{
			set_status(c, "The favourites are full: delete one first.");
			return;
		}
		slot = c->app->nfav++;
	}
	c->app->fav[slot] = f;
	fill_list(c);
	olui_list_select(c->ui, c->favs, c->fav_items[slot]);
	if (fav_save(c->app))
		snprintf(line, sizeof(line), "Saved %s in the favourites.", f.name);
	else
		snprintf(line, sizeof(line), "The favourites could not be written to ENVARC:.");
	set_status(c, line);
}

static void do_delete(struct cw* c)
{
	int i = selected_fav(c);

	if (i < 0)
	{
		set_status(c, "Choose a favourite to delete first.");
		return;
	}
	memmove(&c->app->fav[i], &c->app->fav[i + 1], sizeof(c->app->fav[0]) * (size_t)(c->app->nfav - i - 1));
	c->app->nfav--;
	fill_list(c);
	fav_save(c->app);
	set_status(c, "Deleted the favourite.");
}

/* NLA's result for the status line; the password is never shown */
static void nla_status(struct cw* c, int rc, const struct rdp_nla_result* r)
{
	char line[200];
	const char* text;

	if (rc == RDP_E_NLA && r->gnerr == GNERR_SERVER)
	{
		text = rdp_ntstatus_text(r->server_error);
		if (text)
			snprintf(line, sizeof(line), "Logon refused: %s", text);
		else
			snprintf(line, sizeof(line), "Logon refused (0x%08lX).", (unsigned long)r->server_error);
	}
	else if (rc == RDP_E_NLA && r->early_auth)
		snprintf(line, sizeof(line), "Logon refused: access denied.");
	else if (rc == RDP_E_NLA)
		snprintf(line, sizeof(line), "Logon failed: %s", r->lib_error ? r->lib_error : "?");
	else if (r->closed_after == 2)
		snprintf(line, sizeof(line), "The computer closed the connection: the user name or password may be wrong.");
	else if (rc == AMIPR_ENOLIB)
		snprintf(line, sizeof(line), "gfopenntlm.library is missing from LIBS:.");
	else
		snprintf(line, sizeof(line), "%s", rdp_error_text(rc));
	set_status(c, line);
}

/* the real connection's first steps: TCP, the X.224 negotiation, TLS,
 * and NLA's logon when the computer chose it */
static int probe(struct cw* c, struct session_info* info, ULONG protocols, const char* password)
{
	struct rdp_settings s;
	struct rdp_transport t;
	struct rdp_x224_cc cc;
	char line[160];
	int rc;

	rdp_settings_default(&s);
	s.host = info->host;
	s.user = info->user;
	s.domain = info->domain;
	s.protocols = protocols;
	rdp_transport_init(&t);
	rc = rdp_transport_connect(&t, info->host, s.port, s.timeout_us);
	if (rc == 0)
		rc = rdp_negotiate(&t, &s, &cc);
	if (rc == 0 && cc.has_neg && cc.neg_type == RDP_NEG_FAILURE)
	{
		set_status(c, rdp_negfail_text(cc.failure));
		rdp_transport_close(&t);
		return 0;
	}
	if (rc == 0 && cc.selected == RDP_PROTOCOL_RDP)
	{
		set_status(c, "The computer offers only standard RDP security, which this version does not do.");
		rdp_transport_close(&t);
		return 0;
	}
	if (rc == 0)
	{
		set_status(c, "Making a secure connection...");
		olui_amiga_draw(c->a);
		rc = rdp_transport_start_tls(&t);
	}
	if (rc != 0)
	{
		snprintf(line, sizeof(line), "%s", rdp_error_text(rc));
		set_status(c, line);
		rdp_transport_close(&t);
		return 0;
	}
	if (cc.selected & (RDP_PROTOCOL_HYBRID | RDP_PROTOCOL_HYBRID_EX))
	{
		const uint8_t* der;
		size_t len;
		struct rdp_x509 x;
		struct rdp_nla_result nr;

		if (!info->user[0])
		{
			set_status(c, "This computer needs a user name and password first (NLA).");
			olui_focus(c->ui, c->user);
			rdp_transport_close(&t);
			return 0;
		}
		if (amipr_tls_peer_certificate(t.tls, &der, &len) != 0 || rdp_x509_parse(&x, der, len) != 0)
		{
			set_status(c, "The computer's certificate could not be read.");
			rdp_transport_close(&t);
			return 0;
		}
		set_status(c, "Logging on...");
		olui_amiga_draw(c->a);
		rc = rdp_nla(&t, &s, password, x.pubkey, x.pubkey_len, (cc.selected & RDP_PROTOCOL_HYBRID_EX) != 0, &nr);
		if (rc != 0)
		{
			nla_status(c, rc, &nr);
			rdp_transport_close(&t);
			return 0;
		}
	}
	/* MCS, GCC, the channels and the Client Info PDU, as far as licensing */
	{
		struct rdp_mcs_result mr;
		s.width = info->width;
		s.height = info->height;
		s.depth = (uint8_t)info->depth;
		set_status(c, "Joining the session's channels...");
		olui_amiga_draw(c->a);
		rc = rdp_mcs_connect(&t, &s, cc.selected, password, &mr);
		if (rc != 0)
		{
			if (mr.disconnect_reason)
				snprintf(line, sizeof(line), "The computer ended the connection after %s (reason %u).",
				         rdp_mcs_stage_name(mr.stage), mr.disconnect_reason - 1);
			else if (mr.error_info)
				snprintf(line, sizeof(line), "The computer ended the connection after %s (error 0x%08lX).",
				         rdp_mcs_stage_name(mr.stage), (unsigned long)mr.error_info);
			else
				snprintf(line, sizeof(line), "Stopped after %s: %s", rdp_mcs_stage_name(mr.stage), rdp_error_text(rc));
			set_status(c, line);
			rdp_transport_close(&t);
			return 0;
		}
		snprintf(info->security, sizeof(info->security), "%s, %s %s; reached licensing: %s",
		         cc.selected == RDP_PROTOCOL_SSL ? "TLS" : "NLA",
		         amipr_tls_protocol(t.tls) == 0x0304 ? "TLS 1.3" : "TLS 1.2",
		         amipr_tls_cipher(t.tls) ? amipr_tls_cipher(t.tls) : "",
		         mr.lic_type == 0xFF ? rdp_license_error_text(mr.lic_error) : "licence exchange");
	}
	rdp_transport_close(&t);
	return 1;
}

static void do_connect(struct cw* c)
{
	struct session_info* info = c->info;
	const char* host = olui_get_text(c->host);
	int r, d;
	char line[320];

	if (!host[0])
	{
		set_status(c, "Type the computer's name or address first.");
		olui_focus(c->ui, c->host);
		return;
	}
	memset(info, 0, sizeof(*info));
	copy_text(info->host, sizeof(info->host), host);
	copy_text(info->user, sizeof(info->user), olui_get_text(c->user));
	copy_text(info->domain, sizeof(info->domain), olui_get_text(c->domain));
	r = olui_select_index(c->res);
	d = olui_select_index(c->depth);
	if (r < 0 || r >= ui_nresolutions) r = 1;
	if (d < 0 || d >= ui_ndepths) d = 2;
	info->width = ui_resolutions[r].w ? ui_resolutions[r].w : (UWORD)c->app->screen->Width;
	info->height = ui_resolutions[r].h ? ui_resolutions[r].h : (UWORD)c->app->screen->Height;
	info->depth = ui_depths[d].bits;
	if (c->app->demo)
	{
		info->demo = 1;
		strcpy(info->security, "Demo: no server");
		c->result = 1;
		return;
	}
	snprintf(line, sizeof(line), "Connecting to %s...", info->host);
	set_status(c, line);
	olui_amiga_draw(c->a);
	{
		int s = olui_select_index(c->sec);
		if (s < 0 || s >= ui_nsecurity) s = 0;
		if (probe(c, info, ui_security[s].protocols, olui_get_text(c->password)))
			c->result = 1;
	}
}

static void on_click(olui* ui, const olui_event* ev, void* data)
{
	struct cw* c = data;
	(void)ui;
	if (!strcmp(ev->id, "connect")) do_connect(c);
	else if (!strcmp(ev->id, "cancel")) c->result = 0;
	else if (!strcmp(ev->id, "save")) do_save(c);
	else if (!strcmp(ev->id, "delete")) do_delete(c);
}

static void on_submit(olui* ui, const olui_event* ev, void* data)
{
	(void)ui; (void)ev;
	do_connect((struct cw*)data);
}

static void on_fav(olui* ui, const olui_event* ev, void* data)
{
	struct cw* c = data;
	int i = selected_fav(c);
	(void)ui;
	if (i < 0)
		return;
	show_fav(c, i);
	if (ev->type == OLUI_ACTIVATE)
		do_connect(c);
}

static void on_clip(olui* ui, const olui_event* ev, void* data)
{
	(void)data;
	if (ev->type == OLUI_COPY || ev->type == OLUI_CUT)
		copy_text(s_clip, sizeof(s_clip), ev->value);
	else if (ev->type == OLUI_PASTE && s_clip[0])
		olui_insert_text(ui, ev->el, s_clip);
}

static int is_text(struct cw* c, olui_el* el)
{
	return el && (el == c->host || el == c->user || el == c->domain || el == c->password ||
	              el == c->res || el == c->depth || el == c->sec);
}

/* the underlined keys, when the keyboard is not in a field: 1 if used */
static int shortcut(struct cw* c, UWORD code)
{
	olui_el* focus = olui_focused(c->ui);
	int ch = code >= 'A' && code <= 'Z' ? code + 32 : code;

	if (ch == 27)
	{
		if (olui_select_is_open(c->ui, c->res) || olui_select_is_open(c->ui, c->depth) ||
		    olui_select_is_open(c->ui, c->sec))
			return 0; /* Esc closes the open list first */
		c->result = 0;
		return 1;
	}
	if (is_text(c, focus))
		return 0;
	switch (ch)
	{
		case 'm': olui_focus(c->ui, c->host); return 1;
		case 'u': olui_focus(c->ui, c->user); return 1;
		case 'd': olui_focus(c->ui, c->domain); return 1;
		case 'p': olui_focus(c->ui, c->password); return 1;
		case 'r': olui_focus(c->ui, c->res); return 1;
		case 'l': olui_focus(c->ui, c->depth); return 1;
		case 'y': olui_focus(c->ui, c->sec); return 1;
		case 'f': olui_focus(c->ui, c->favs); return 1;
		case 's': do_save(c); return 1;
		case 'e': do_delete(c); return 1;
		case 'c': do_connect(c); return 1;
		case 'a': c->result = 0; return 1;
		default: return 0;
	}
}

static void menu_pick(struct cw* c, UWORD code)
{
	struct Menu* strip = c->win->MenuStrip;

	while (code != MENUNULL && c->result < 0)
	{
		struct MenuItem* item = ItemAddress(strip, code);
		if (!item)
			break;
		switch ((ULONG)GTMENUITEM_USERDATA(item))
		{
			case M_CONNECT: do_connect(c); break;
			case M_SAVE: do_save(c); break;
			case M_DELETE: do_delete(c); break;
			case M_QUIT: c->result = 0; break;
			case M_CUT: olui_edit(c->ui, OLUI_CUT); break;
			case M_COPY: olui_edit(c->ui, OLUI_COPY); break;
			case M_PASTE: olui_edit(c->ui, OLUI_PASTE); break;
			case M_ABOUT: ui_about(c->win); break;
		}
		code = item->NextSelect;
	}
}


int connect_window(struct app* app, const char* host, struct session_info* info)
{
	struct cw c;
	struct Menu* menu;
	char err[128];
	UWORD fh = app->screen->Font ? app->screen->Font->ta_YSize : 8;
	int w = fh * 70, h = fh * 36;

	memset(&c, 0, sizeof(c));
	c.app = app;
	c.info = info;
	c.result = -1;
	if (w < 560) w = 560;
	if (h < 300) h = 300;
	if (w > app->screen->Width) w = app->screen->Width;
	if (h > app->screen->Height) h = app->screen->Height;

	c.win = OpenWindowTags(NULL,
		WA_Title, (ULONG)APP_NAME " \xB7 New connection",
		WA_ScreenTitle, (ULONG)APP_NAME " " APP_VERSION,
		WA_PubScreen, (ULONG)app->screen,
		WA_Left, (app->screen->Width - w) / 2, WA_Top, (app->screen->Height - h) / 2,
		WA_Width, w, WA_Height, h, WA_MinWidth, 420, WA_MinHeight, 240,
		WA_MaxWidth, ~0UL, WA_MaxHeight, ~0UL,
		WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
		WA_Activate, TRUE, WA_SimpleRefresh, TRUE, WA_ReportMouse, TRUE, WA_NewLookMenus, TRUE,
		WA_IDCMP, olui_amiga_idcmp() | IDCMP_MENUPICK, TAG_DONE);
	if (!c.win)
		return 0;
	menu = ui_menus(app, menus);
	if (menu)
		SetMenuStrip(c.win, menu);

	if ((c.ui = olui_new()) && (c.a = olui_amiga_new(c.ui, c.win)))
	{
		olui_amiga_theme(c.a, NULL);
		if (olui_load(c.ui, ui_connect_markup(), ui_connect_css, err, sizeof(err)))
		{
			c.host = olui_find(c.ui, "host");
			c.user = olui_find(c.ui, "user");
			c.domain = olui_find(c.ui, "domain");
			c.password = olui_find(c.ui, "password");
			c.res = olui_find(c.ui, "res");
			c.depth = olui_find(c.ui, "depth");
			c.sec = olui_find(c.ui, "sec");
			c.favs = olui_find(c.ui, "favs");
			c.status = olui_find(c.ui, "status");
			olui_select_set(c.ui, c.res, 1);
			olui_select_set(c.ui, c.depth, 2);
			fill_list(&c);
			if (info->user[0]) olui_set_text(c.ui, c.user, info->user);
			if (info->domain[0]) olui_set_text(c.ui, c.domain, info->domain);
			if (host && *host)
				olui_set_text(c.ui, c.host, host);
			else if (app->nfav > 0)
			{
				olui_list_select(c.ui, c.favs, c.fav_items[0]);
				show_fav(&c, 0);
			}
			if (app->demo)
				set_status(&c, "Demo: no server is needed. Connect opens a demo session.");
			olui_on(c.ui, NULL, OLUI_CLICK, on_click, &c);
			olui_on(c.ui, NULL, OLUI_SUBMIT, on_submit, &c);
			olui_on(c.ui, "favs", OLUI_SELECT, on_fav, &c);
			olui_on(c.ui, "favs", OLUI_ACTIVATE, on_fav, &c);
			olui_on(c.ui, NULL, OLUI_COPY, on_clip, &c);
			olui_on(c.ui, NULL, OLUI_CUT, on_clip, &c);
			olui_on(c.ui, NULL, OLUI_PASTE, on_clip, &c);
			olui_focus(c.ui, c.host);
			if (olui_amiga_layout(c.a))
			{
				while (c.result < 0)
				{
					struct IntuiMessage* m;
					ULONG sig = Wait((1UL << c.win->UserPort->mp_SigBit) | SIGBREAKF_CTRL_C);
					if (sig & SIGBREAKF_CTRL_C)
						c.result = 0;
					while (c.result < 0 && (m = (struct IntuiMessage*)GetMsg(c.win->UserPort)))
					{
						if (m->Class == IDCMP_MENUPICK)
						{
							UWORD code = m->Code;
							ReplyMsg(&m->ExecMessage);
							menu_pick(&c, code);
							continue;
						}
						if (m->Class == IDCMP_VANILLAKEY &&
						    !(m->Qualifier & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND)) &&
						    shortcut(&c, m->Code))
						{
							ReplyMsg(&m->ExecMessage);
							continue;
						}
						if (olui_amiga_event(c.a, m) == OLUI_AMIGA_CLOSE)
							c.result = 0;
						ReplyMsg(&m->ExecMessage);
					}
					olui_amiga_draw(c.a);
				}
			}
		}
		else
			Printf((CONST_STRPTR)APP_NAME ": %s\n", (ULONG)err);
	}
	olui_amiga_free(c.a);
	olui_free(c.ui);
	if (menu)
	{
		ClearMenuStrip(c.win);
		FreeMenus(menu);
	}
	CloseWindow(c.win);
	return c.result > 0;
}
