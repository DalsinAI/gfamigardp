/*
 * GFAmigaRDP: lays the program's OpenLayout UI windows out on a PC with
 * the same markup, CSS and theme, and writes their draw ops (for
 * GFOpenLayout's tests/ui_render.py) and the desktop's test pattern (a
 * PPM). A look at the layout before it reaches an Amiga, not the Amiga's
 * drawing: text is measured in DejaVu Sans, the font ui_render.py uses.
 *
 *   preview_ui connect THEME OPS [WIDTH HEIGHT]
 *   preview_ui session THEME OPS PPM [WIDTH HEIGHT]
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "gfopenlayout_ui.h"
#include "markup.h"
#include "widths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int measure(void* userdata, const char* utf8, size_t length, const ol_style* style,
                   ol_text_metrics* m)
{
	size_t i, glyphs = 0;
	(void)userdata;
	for (i = 0; i < length; ++i)
		if (((unsigned char)utf8[i] & 0xc0u) != 0x80u)
			++glyphs;
	{
		/* DejaVu Sans advances in 1000ths of an em (widths.h, made by preview.sh), what ui_render.py draws with */
		long sum = 0;
		const int* tab = (style->text_flags & OL_TEXT_BOLD) ? dv_bold : dv_book;
		(void)glyphs;
		for (i = 0; i < length; ++i)
		{
			unsigned char ch = (unsigned char)utf8[i];
			if ((ch & 0xc0u) == 0x80u)
				continue;
			/* each character rounded on its own: a run's width is then the sum
			 * of its pieces', however the layout splits it */
			sum += ((ch >= 32 && ch < 127 ? tab[ch - 32] : 600) * (long)style->font_size + 500) / 1000;
		}
		m->width = (ol_unit)sum;
	}
	m->ascent = style->font_size * 10 / 13;
	m->descent = style->font_size * 3 / 13;
	return 1;
}

struct sink { FILE* out; long dy; };

static void draw(void* userdata, const olui_draw_op* op)
{
	struct sink* s = userdata;
	int i;
	switch (op->kind)
	{
		case OLUI_DRAW_BOX:
			fprintf(s->out, "box %ld %ld %ld %ld %d %08lx %d %d", (long)op->box.x, (long)op->box.y + s->dy,
			        (long)op->box.w, (long)op->box.h, op->border, (unsigned long)op->border_argb, op->radius,
			        op->stops);
			for (i = 0; i < op->stops; ++i)
				fprintf(s->out, " %08lx %d", (unsigned long)op->stop_argb[i], op->stop_pos[i]);
			fputc('\n', s->out);
			break;
		case OLUI_DRAW_TEXT:
			fprintf(s->out, "text %ld %ld %d %08lx %lu %.*s\n", (long)op->box.x, (long)op->baseline + s->dy,
			        op->font_pixels, (unsigned long)op->argb, (unsigned long)op->text_flags, (int)op->length,
			        op->text);
			break;
		case OLUI_DRAW_MARK:
			fprintf(s->out, "mark %ld %ld %ld %ld %d %08lx\n", (long)op->box.x, (long)op->box.y + s->dy,
			        (long)op->box.w, (long)op->box.h, op->mark, (unsigned long)op->argb);
			break;
		case OLUI_DRAW_CLIP:
			fprintf(s->out, "clip %ld %ld %ld %ld\n", (long)op->clip.x, (long)op->clip.y + s->dy,
			        (long)op->clip.w, (long)op->clip.h);
			break;
		default:
			break;
	}
}

static char* slurp(const char* path)
{
	FILE* f = fopen(path, "rb");
	long n;
	char* b;
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc((size_t)n + 1);
	if (b && fread(b, 1, (size_t)n, f) != (size_t)n)
	{
		free(b);
		b = NULL;
	}
	if (b)
		b[n] = 0;
	fclose(f);
	return b;
}

static olui* make(const char* theme, const char* markup, const char* css)
{
	olui* ui = olui_new();
	char err[128];
	char* t = slurp(theme);
	olui_set_measure(ui, measure, NULL);
	if (t)
	{
		char* c = olui_theme_css(t, 0, err, sizeof(err));
		if (c)
		{
			olui_add_css(ui, c);
			olui_free_css(c);
		}
		else
			fprintf(stderr, "theme: %s\n", err);
		free(t);
	}
	if (!olui_load(ui, markup, css, err, sizeof(err)))
	{
		fprintf(stderr, "markup: %s\n", err);
		exit(1);
	}
	return ui;
}

static void dump(olui* ui, FILE* out, long dy)
{
	struct sink s;
	s.out = out;
	s.dy = dy;
	olui_draw(ui, NULL, draw, &s);
}

int main(int argc, char** argv)
{
	FILE* out;
	if (argc < 4)
	{
		fprintf(stderr, "preview_ui connect|session THEME OPS [PPM] [W H]\n");
		return 2;
	}
	out = fopen(argv[3], "w");
	if (!out)
		return 1;
	if (!strcmp(argv[1], "connect"))
	{
		int w = argc > 5 ? atoi(argv[4]) : 600, h = argc > 5 ? atoi(argv[5]) : 330, i;
		olui* ui = make(argv[2], ui_connect_markup(), ui_connect_css);
		static const char* fav[] = { "Office desktop", "Build server", "Home media PC", "Lab: Windows 11" };
		olui_el* list = olui_find(ui, "favs");
		olui_el* first = NULL;
		for (i = 0; i < 4; i++)
		{
			olui_el* it = olui_list_add(ui, list, NULL, fav[i], NULL);
			if (!first) first = it;
		}
		olui_list_select(ui, list, first);
		olui_set_text(ui, olui_find(ui, "host"), "desktop01.example.org");
		olui_set_text(ui, olui_find(ui, "user"), "sam");
		olui_set_text(ui, olui_find(ui, "domain"), "EXAMPLE");
		olui_select_set(ui, olui_find(ui, "res"), 2);
		olui_select_set(ui, olui_find(ui, "depth"), 2);
		olui_set_text(ui, olui_find(ui, "status"), "Demo: no server is needed. Connect opens a demo session.");
		olui_focus(ui, olui_find(ui, "password"));
		olui_layout(ui, w, h);
		fprintf(out, "size %d %d\n", w, h);
		dump(ui, out, 0);
		olui_free(ui);
	}
	else
	{
		int W = argc > 6 ? atoi(argv[5]) : 800, H = argc > 6 ? atoi(argv[6]) : 600;
		int tb = 8 + 13 + 12, sb = 8 + 13 + 4, x, y;
		olui* t = make(argv[2], ui_toolbar_markup, ui_toolbar_css);
		olui* s = make(argv[2], ui_status_markup, ui_status_css);
		FILE* ppm;
		olui_set_text(t, olui_find(t, "where"), "sam on desktop01.example.org");
		olui_set_text(s, olui_find(s, "status"), "Demo session: no server. Amiga-D disconnects.");
		olui_set_text(s, olui_find(s, "mode"), "800 \xC3\x97 600, 16-bit \xC2\xB7 Demo: no server");
		olui_layout(t, W, tb);
		olui_layout(s, W, sb);
		fprintf(out, "size %d %d\n", W, H + tb + sb);
		dump(t, out, 0);
		dump(s, out, tb + H);
		if (argc < 5 || !(ppm = fopen(argv[4], "wb")))
			return 1;
		fprintf(ppm, "P6\n%d %d\n255\n", W, H);
		for (y = 0; y < H; y++)
			for (x = 0; x < W; x++)
			{
				uint32_t c = ui_pattern(x, y, W, H);
				fputc((int)(c >> 16) & 255, ppm);
				fputc((int)(c >> 8) & 255, ppm);
				fputc((int)c & 255, ppm);
			}
		fclose(ppm);
		printf("toolbar %d status %d\n", tb, sb);
		olui_free(t);
		olui_free(s);
	}
	fclose(out);
	return 0;
}
