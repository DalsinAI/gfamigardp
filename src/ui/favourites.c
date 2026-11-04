/*
 * GFAmigaRDP: favourites, kept in ENVARC:GFAmigaRDP/Favourites (copied to
 * ENV: at boot like every ENVARC: file), one connection a line with a tab
 * between fields: name, host, user, domain, resolution, colours,
 * security. The password is never written.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "ui.h"

#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAV_DIR "ENVARC:GFAmigaRDP"
#define FAV_FILE "ENVARC:GFAmigaRDP/Favourites"
#define FAV_ENV "ENV:GFAmigaRDP/Favourites"

static void copy_field(char* dst, size_t size, const char* src, size_t len)
{
	if (len >= size)
		len = size - 1;
	memcpy(dst, src, len);
	dst[len] = 0;
}

static int clamp_index(const char* s, int n)
{
	int v = atoi(s);
	return v < 0 || v >= n ? 0 : v;
}

static int parse_line(char* line, struct favourite* f)
{
	char* part[7];
	int n = 0;
	char* p = line;

	memset(f, 0, sizeof(*f));
	while (n < 7)
	{
		part[n++] = p;
		p = strchr(p, '\t');
		if (!p)
			break;
		*p++ = 0;
	}
	if (n < 2 || !part[0][0] || !part[1][0])
		return 0;
	copy_field(f->name, sizeof(f->name), part[0], strlen(part[0]));
	copy_field(f->host, sizeof(f->host), part[1], strlen(part[1]));
	if (n > 2) copy_field(f->user, sizeof(f->user), part[2], strlen(part[2]));
	if (n > 3) copy_field(f->domain, sizeof(f->domain), part[3], strlen(part[3]));
	f->res = n > 4 ? clamp_index(part[4], ui_nresolutions) : 1;
	f->depth = n > 5 ? clamp_index(part[5], ui_ndepths) : 2;
	f->security = n > 6 ? clamp_index(part[6], ui_nsecurity) : 0;
	return 1;
}

int fav_load(struct app* app)
{
	BPTR fh;
	char line[512];

	app->nfav = 0;
	fh = Open((CONST_STRPTR)FAV_ENV, MODE_OLDFILE);
	if (!fh)
		fh = Open((CONST_STRPTR)FAV_FILE, MODE_OLDFILE);
	if (!fh)
		return 0;
	while (app->nfav < MAX_FAVOURITES && FGets(fh, (STRPTR)line, sizeof(line)))
	{
		size_t l = strlen(line);
		while (l && (line[l - 1] == '\n' || line[l - 1] == '\r'))
			line[--l] = 0;
		if (line[0] == '#' || !line[0])
			continue;
		if (parse_line(line, &app->fav[app->nfav]))
			app->nfav++;
	}
	Close(fh);
	return app->nfav;
}

static int write_to(const char* path, const struct app* app)
{
	BPTR fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
	char line[600];
	int i;

	if (!fh)
		return 0;
	FPuts(fh, (CONST_STRPTR) "# GFAmigaRDP favourites: name, host, user, domain, resolution, colours, security\n");
	for (i = 0; i < app->nfav; i++)
	{
		const struct favourite* f = &app->fav[i];
		snprintf(line, sizeof(line), "%s\t%s\t%s\t%s\t%d\t%d\t%d\n", f->name, f->host, f->user,
		         f->domain, f->res, f->depth, f->security);
		FPuts(fh, (CONST_STRPTR)line);
	}
	Close(fh);
	return 1;
}

int fav_save(const struct app* app)
{
	BPTR l;

	if (app->demo)
		return 1; /* the demo's favourites are not the user's: nothing is written */
	l = Lock((CONST_STRPTR)FAV_DIR, SHARED_LOCK);
	if (!l)
		l = CreateDir((CONST_STRPTR)FAV_DIR);
	if (l)
		UnLock(l);
	l = Lock((CONST_STRPTR) "ENV:GFAmigaRDP", SHARED_LOCK);
	if (!l)
		l = CreateDir((CONST_STRPTR) "ENV:GFAmigaRDP");
	if (l)
		UnLock(l);
	write_to(FAV_ENV, app);
	return write_to(FAV_FILE, app);
}

void fav_demo(struct app* app)
{
	static const struct favourite demo[] = {
		{ "Office desktop", "desktop01.example.org", "sam", "EXAMPLE", 2, 2, 0 },
		{ "Build server", "build.example.org", "builder", "", 1, 2, 1 },
		{ "Home media PC", "192.168.1.20", "sam", "", 1, 3, 0 },
		{ "Lab: Windows 11", "lab-win11.example.org", "tester", "LAB", 3, 3, 2 },
	};
	unsigned i;

	app->nfav = 0;
	for (i = 0; i < sizeof(demo) / sizeof(demo[0]); i++)
		app->fav[app->nfav++] = demo[i];
}
