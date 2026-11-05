/*
 * amipr: clipboard.device FTXT/CHRS; see amipr_clip.h.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "amipr_clip.h"
#include "amipr.h"
#include "amipr_internal.h"
#include "utf16.h"

#include <exec/memory.h>
#include <exec/ports.h>
#include <exec/io.h>
#include <devices/clipboard.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

#define ID(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
#define ID_FORM ID('F', 'O', 'R', 'M')
#define ID_FTXT ID('F', 'T', 'X', 'T')
#define ID_CHRS ID('C', 'H', 'R', 'S')
#define MAX_CLIP (256u * 1024u)

static struct MsgPort* s_port;
static struct IOClipReq* s_io;
static int s_open;

static LONG clip_rw(UWORD cmd, void* buf, LONG len)
{
	s_io->io_Command = cmd;
	s_io->io_Data = (STRPTR)buf;
	s_io->io_Length = (ULONG)len;
	DoIO((struct IORequest*)s_io);
	return s_io->io_Error ? -1 : (LONG)s_io->io_Actual;
}

static void clip_drain(void)
{
	UBYTE dummy[64];
	while (clip_rw(CMD_READ, dummy, (LONG)sizeof(dummy)) > 0)
		;
}

int amipr_clip_init(void)
{
	if (s_open)
		return AMIPR_OK;
	if (amipr_init() < 0)
		return AMIPR_ENOLIB;
	s_port = CreateMsgPort();
	if (!s_port)
		return AMIPR_ENOMEM;
	s_io = (struct IOClipReq*)CreateIORequest(s_port, sizeof(*s_io));
	if (!s_io)
	{
		DeleteMsgPort(s_port);
		s_port = NULL;
		return AMIPR_ENOMEM;
	}
	if (OpenDevice((CONST_STRPTR)"clipboard.device", PRIMARY_CLIP, (struct IORequest*)s_io, 0))
	{
		DeleteIORequest((struct IORequest*)s_io);
		DeleteMsgPort(s_port);
		s_io = NULL;
		s_port = NULL;
		return AMIPR_ENOLIB;
	}
	s_open = 1;
	return AMIPR_OK;
}

void amipr_clip_shutdown(void)
{
	if (!s_open)
		return;
	CloseDevice((struct IORequest*)s_io);
	DeleteIORequest((struct IORequest*)s_io);
	DeleteMsgPort(s_port);
	s_io = NULL;
	s_port = NULL;
	s_open = 0;
}

int32_t amipr_clip_read_id(void)
{
	if (!s_open && amipr_clip_init() < 0)
		return 0;
	s_io->io_Command = CBD_CURRENTREADID;
	DoIO((struct IORequest*)s_io);
	return (int32_t)s_io->io_ClipID;
}

int amipr_clip_get_text(char* dst, size_t size)
{
	uint32_t head[3], chunk[2];
	UBYTE* raw;
	ULONG total = 0;
	char* tmp;

	if (!dst || size == 0)
		return AMIPR_EINVAL;
	dst[0] = 0;
	if (!s_open && amipr_clip_init() < 0)
		return AMIPR_ENOLIB;

	s_io->io_Offset = 0;
	s_io->io_ClipID = 0;
	if (clip_rw(CMD_READ, head, 12) != 12 || head[0] != ID_FORM || head[2] != ID_FTXT ||
	    head[1] > MAX_CLIP + 64)
	{
		clip_drain();
		return AMIPR_ENOTSUP;
	}
	raw = (UBYTE*)AllocVec(head[1] + 1, MEMF_ANY);
	if (!raw)
	{
		clip_drain();
		return AMIPR_ENOMEM;
	}
	while (clip_rw(CMD_READ, chunk, 8) == 8)
	{
		ULONG sz = chunk[1], padded = sz + (sz & 1);
		if (chunk[0] == ID_CHRS && total + sz <= head[1])
		{
			if (clip_rw(CMD_READ, raw + total, (LONG)sz) != (LONG)sz)
				break;
			total += sz;
			if (sz & 1)
			{
				UBYTE pad;
				clip_rw(CMD_READ, &pad, 1);
			}
		}
		else
		{
			UBYTE skip[64];
			while (padded)
			{
				LONG n = padded < sizeof(skip) ? (LONG)padded : (LONG)sizeof(skip);
				if (clip_rw(CMD_READ, skip, n) != n)
					break;
				padded -= (ULONG)n;
			}
		}
	}
	clip_drain();
	raw[total] = 0;

	tmp = (char*)AllocVec(MAX_CLIP + 1, MEMF_ANY);
	if (!tmp)
	{
		FreeVec(raw);
		return AMIPR_ENOMEM;
	}
	rdp_amiga_to_utf8(tmp, MAX_CLIP + 1, (const char*)raw);
	FreeVec(raw);
	rdp_copy_utf8(dst, size, tmp);
	FreeVec(tmp);
	return AMIPR_OK;
}

int amipr_clip_set_text(const char* utf8)
{
	uint32_t form[3], chunk[2];
	UBYTE pad = 0;
	UBYTE* latin;
	size_t i = 0, n;
	const uint8_t* p;

	if (!s_open && amipr_clip_init() < 0)
		return AMIPR_ENOLIB;
	if (!utf8)
		utf8 = "";

	/* UTF-8 → Latin-1 for CHRS (lossy outside 0..255). */
	n = strlen(utf8);
	if (n > MAX_CLIP)
		n = MAX_CLIP;
	latin = (UBYTE*)AllocVec((ULONG)(n + 1), MEMF_ANY);
	if (!latin)
		return AMIPR_ENOMEM;
	p = (const uint8_t*)utf8;
	while (*p && i < n)
	{
		uint32_t cp = rdp_utf8_next(&p);
		latin[i++] = (UBYTE)(cp < 0x100 ? cp : '?');
	}
	latin[i] = 0;
	n = i;

	form[0] = ID_FORM;
	form[1] = 4 + 8 + (uint32_t)n + (n & 1);
	form[2] = ID_FTXT;
	chunk[0] = ID_CHRS;
	chunk[1] = (uint32_t)n;
	s_io->io_Offset = 0;
	s_io->io_ClipID = 0;
	s_io->io_Error = 0;
	clip_rw(CMD_WRITE, form, 12);
	clip_rw(CMD_WRITE, chunk, 8);
	if (n)
		clip_rw(CMD_WRITE, latin, (LONG)n);
	if (n & 1)
		clip_rw(CMD_WRITE, &pad, 1);
	s_io->io_Command = CMD_UPDATE;
	DoIO((struct IORequest*)s_io);
	FreeVec(latin);
	return s_io->io_Error ? AMIPR_EDOS : AMIPR_OK;
}
