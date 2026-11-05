/*
 * GFAmigaRDP: the program's windows (OpenLayout UI, docs/DESIGN.md 7).
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_UI_H
#define RDP_UI_H

#include <exec/types.h>
#include <stdint.h>
#include <intuition/intuition.h>

#include "markup.h"
#include "arc.h"

struct rdp_conn;

#define APP_NAME "GFAmigaRDP"
#define APP_VERSION "0.1"

/* one saved connection; the password is never saved */
struct favourite
{
	char name[64];
	char host[256];
	char user[256];
	char domain[256];
	int res;      /* index into ui_resolutions */
	int depth;    /* index into ui_depths */
	int security; /* index into ui_security */
};

#define MAX_FAVOURITES 32


/* what a session window is asked to show */
struct session_info
{
	char host[256];
	char user[256];
	char domain[256];
	UWORD width, height;
	UBYTE depth;
	char security[128];  /* the negotiated protocol, the TLS cipher and how far it got, in words */
	int demo;            /* 1: no server, a test pattern */
	struct rdp_conn* conn;   /* the app's: open (live) after licensing, for the session window */
	/* Remembered for cookie reconnect (never a password): */
	uint16_t port;
	uint32_t protocols;
	uint16_t early_caps, color_depths;
	uint8_t gfx;
	struct rdp_arc_cookie arc; /* from Save Session Info; wiped on Disconnect */
	int want_reconnect;       /* session asks connect_window to cookie-reconnect */
};

struct app
{
	struct Screen* screen;     /* the public screen (Workbench), locked */
	APTR visual;               /* GadTools' VisualInfo for the menus */
	int demo;
	struct favourite fav[MAX_FAVOURITES];
	int nfav;
	struct rdp_conn* conn;     /* the one connection (session.h), AllocVec'd in main */
};

/* favourites.c: ENVARC:GFAmigaRDP/Favourites, one per line, tab between fields */
int fav_load(struct app* app);
int fav_save(const struct app* app);
void fav_demo(struct app* app);

/* connect_win.c: answers 1 when a session window should open (info filled), 0 to quit */
int connect_window(struct app* app, const char* host, struct session_info* info);
/* End the connection task (and its bsdsocket base) after the session. */
void ui_ct_shutdown(void);

/* session_win.c: answers 1 to go back to the connection window, 0 to quit */
int session_window(struct app* app, struct session_info* info);

/* desktop.c: the desktop area: a test pattern until the RDP output draws there */
void desktop_draw(struct Window* win, int x, int y, int w, int h, const struct session_info* info);
void desktop_free(void);
/* the remote desktop from the Graphics Output Buffer (gfx_fb.h): only
 * rects (fb coordinates), or all of it; clipped to the w x h band */
struct rdp_fb;
struct rdp_rect;
/* ox, oy: top-left of the band in framebuffer coordinates (pan). */
void desktop_blit(struct Window* win, int x, int y, int w, int h, const struct rdp_fb* fb,
                  const struct rdp_rect* rects, int n, int ox, int oy);
void desktop_draw_fb(struct Window* win, int x, int y, int w, int h, const struct rdp_fb* fb,
                     int ox, int oy);

struct NewMenu;
/* menus: GadTools, Amiga-key shortcuts */
struct Menu* ui_menus(struct app* app, const struct NewMenu* nm);
void ui_about(struct Window* win);

#endif
