/*
 * GFAmigaRDP host replay: an RDPGFX capture (RDPProbe CAPTURE=file: 4-byte
 * big-endian length, then that message) fed through rdp_gfx_input as the
 * session does, with the session window's view calls after each message
 * (set_view on a WxH band at OX,OY, then paint_view), under ASan/UBSan.
 *   cc -fsanitize=address,undefined -g -I../../src/core -I../../src/codec -o replay_gfx replay_gfx.c \
 *      ../../src/core/rdpgfx.c ../../src/codec/gfx_fb.c ../../src/codec/zgfx.c \
 *      ../../src/codec/planar.c ../../src/codec/progressive.c ../../src/codec/clearcodec.c
 *   ./replay_gfx capture.bin [W H [OX OY]]     (PAN=1: the band moves every message)
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rdpgfx.h"
#include "gfx_fb.h"

static int sink(void* user, const uint8_t* p, size_t n) { (void)user; (void)p; (void)n; return 0; }

int main(int argc, char** argv)
{
	FILE* f;
	static struct rdp_gfx g;
	int w = argc > 3 ? atoi(argv[2]) : 800, h = argc > 3 ? atoi(argv[3]) : 560;
	int ox = argc > 5 ? atoi(argv[4]) : 0, oy = argc > 5 ? atoi(argv[5]) : 0;
	unsigned long msgs = 0, bytes = 0;
	if (argc < 2 || !(f = fopen(argv[1], "rb"))) { fprintf(stderr, "usage: replay_gfx capture [W H [OX OY]]\n"); return 2; }
	if (rdp_gfx_init(&g, NULL, 800, 600, sink, NULL) < 0) return 2;
	for (;;) {
		unsigned char hd[4];
		size_t n;
		uint8_t* buf;
		if (fread(hd, 1, 4, f) != 4) break;
		n = ((size_t)hd[0] << 24) | ((size_t)hd[1] << 16) | ((size_t)hd[2] << 8) | hd[3];
		if (!(buf = malloc(n ? n : 1)) || fread(buf, 1, n, f) != n) { free(buf); break; }
		rdp_gfx_input(&g, buf, n);
		free(buf);
		msgs++; bytes += n;
		if (getenv("PAN")) {   /* pan the band across the desktop every message */
			ox = (int)((msgs * 397) % (g.fb.w > (unsigned)w ? g.fb.w - w : 1));
			oy = (int)((msgs * 211) % (g.fb.h > (unsigned)h ? g.fb.h - h : 1));
		}
		rdp_gfx_set_view(&g, (uint16_t)ox, (uint16_t)oy, (uint16_t)(ox + w), (uint16_t)(oy + h));
		rdp_gfx_paint_view(&g);
	}
	fclose(f);
	printf("replayed %lu messages, %lu bytes; fb %ux%u; frames %u, pdus %u, errors %u\n", msgs, bytes,
	       (unsigned)g.fb.w, (unsigned)g.fb.h, (unsigned)g.st.frames, (unsigned)g.st.pdus, (unsigned)g.st.errors);
	rdp_gfx_free(&g);
	return 0;
}
