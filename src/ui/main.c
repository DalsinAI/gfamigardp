/*
 * GFAmigaRDP: a remote desktop client for AmigaOS 3.2, written from
 * Microsoft's open RDP specifications (README.md, CLEAN-ROOM.md).
 *
 *   GFAmigaRDP [HOST] [USER=name] [DOMAIN=name] [DEMO] [SESSION]
 *
 * With no arguments the connection window opens. DEMO fills it with
 * sample favourites and makes Connect open a session window with a test
 * pattern, so the windows can be seen with no server; SESSION (with DEMO)
 * opens that session window straight away.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "ui.h"
#include "amipr.h"

#include <dos/dos.h>
#include <dos/rdargs.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <stdio.h>
#include <string.h>

static const char version[] = "$VER: " APP_NAME " " APP_VERSION " (04.11.2026) AmiCradle Team";


struct Menu* ui_menus(struct app* app, const struct NewMenu* nm)
{
	struct Menu* m = CreateMenus((struct NewMenu*)nm, GTMN_FrontPen, 0, TAG_DONE);
	if (m && !LayoutMenus(m, app->visual, GTMN_NewLookMenus, TRUE, TAG_DONE))
	{
		FreeMenus(m);
		m = NULL;
	}
	return m;
}

void ui_about(struct Window* win)
{
	struct EasyStruct es = {
		sizeof(struct EasyStruct), 0, (UBYTE*)"About " APP_NAME,
		(UBYTE*)APP_NAME " " APP_VERSION "\n"
		        "A remote desktop client for AmigaOS 3.2.\n\n"
		        "Team 6 (Sacredtrees), based on The Open SDK\n"
		        "by the AmiCradle Team. MIT licence.\n"
		        "Written from Microsoft's open RDP specifications.",
		(UBYTE*)"OK",
	};
	EasyRequestArgs(win, &es, NULL, NULL);
}

#define TEMPLATE "HOST,USER/K,DOMAIN/K,DEMO/S,SESSION/S"
enum { ARG_HOST, ARG_USER, ARG_DOMAIN, ARG_DEMO, ARG_SESSION, ARG_COUNT };

int main(void)
{
	LONG args[ARG_COUNT] = { 0 };
	struct RDArgs* rd;
	struct app app;
	struct session_info info;
	int rc = RETURN_OK, go;

	(void)version;
	memset(&app, 0, sizeof(app));
	rd = ReadArgs((CONST_STRPTR)TEMPLATE, args, NULL);
	if (!rd)
	{
		PrintFault(IoErr(), (CONST_STRPTR)APP_NAME);
		return RETURN_ERROR;
	}
	if (amipr_init() != AMIPR_OK)
	{
		PutStr((CONST_STRPTR)APP_NAME ": timer.device could not be opened\n");
		FreeArgs(rd);
		return RETURN_FAIL;
	}
	app.demo = args[ARG_DEMO] != 0;
	app.screen = LockPubScreen(NULL);
	if (!app.screen || !(app.visual = GetVisualInfo(app.screen, TAG_DONE)))
	{
		PutStr((CONST_STRPTR)APP_NAME ": the Workbench screen is not open\n");
		rc = RETURN_FAIL;
		goto out;
	}
	if (app.demo)
		fav_demo(&app);
	else
		fav_load(&app);

	memset(&info, 0, sizeof(info));
	/* USER/K and DOMAIN/K fill the window's fields (and the demo session's title) */
	if (args[ARG_USER])
		snprintf(info.user, sizeof(info.user), "%s", (const char*)args[ARG_USER]);
	if (args[ARG_DOMAIN])
		snprintf(info.domain, sizeof(info.domain), "%s", (const char*)args[ARG_DOMAIN]);
	go = 0;
	if (app.demo && args[ARG_SESSION])
	{
		snprintf(info.host, sizeof(info.host), "%s", args[ARG_HOST] ? (const char*)args[ARG_HOST] : "desktop01.example.org");
		if (!info.user[0])
			strcpy(info.user, "sam");
		info.width = 800;
		info.height = 600;
		info.depth = 16;
		strcpy(info.security, "Demo: no server");
		info.demo = 1;
		go = 1;
	}
	else
		go = connect_window(&app, (const char*)args[ARG_HOST], &info);
	while (go)
	{
		if (!session_window(&app, &info))
			break;
		go = connect_window(&app, info.host, &info);
	}

out:
	desktop_free();
	if (app.visual)
		FreeVisualInfo(app.visual);
	if (app.screen)
		UnlockPubScreen(NULL, app.screen);
	amipr_net_close();
	FreeArgs(rd);
	return rc;
}
