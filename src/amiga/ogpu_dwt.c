/*
 * Progressive tile inverse DWT via OGPU_OP_DWT_INV.
 *
 * Bit-exact with rdp_prog_idwt / [MS-RDPRFX] 3.1.8.2.4 when the OpenGPU CPU
 * (or later ACRTG) back end answers Query. Falls back (returns 0) when the
 * library is missing, Query says NONE, or Submit fails — Progressive then
 * uses the in-tree CPU idwt.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "ogpu_dwt.h"
#include "progressive.h"

#include <proto/exec.h>
#include <string.h>

#include <gfopengpu/gfopengpu.h>
#include <gfopengpu/stream.h>

/* Shared with desktop.c / ogpu_yuv.c */
extern struct Library* OpenGPUBase;

#include <proto/gfopengpu.h>

static int s_ready;
static ULONG s_stream[16];

static int idwt_tile(void* user, int32_t* buf, int re, int32_t* tmp)
{
	ULONG *w, fence = 0, flags;
	(void)user;
	if (!s_ready || !buf || !tmp || !OpenGPUBase)
		return 0;
	flags = re ? OGPU_DWT_RFX_RE : 0;
	w = s_stream;
	*w++ = OGPU_HDR(OGPU_OP_DWT_INV, 6);
	*w++ = (ULONG)buf;
	*w++ = 0; /* inplace */
	*w++ = flags;
	*w++ = (ULONG)tmp;
	*w++ = OGPU_DWT_RFX_WORK_BYTES;
	*w++ = OGPU_HDR(OGPU_OP_FENCE, 2);
	*w++ = 1;
	if (OGPU_Submit(s_stream, (ULONG)(w - s_stream), &fence) != OGPU_OK)
		return 0;
	OGPU_Wait(fence);
	return 1;
}

void rdp_ogpu_dwt_install(void)
{
	ULONG q;
	s_ready = 0;
	if (!OpenGPUBase)
		OpenGPUBase = OpenLibrary((CONST_STRPTR)OPENGPU_NAME, OPENGPU_VERSION);
	if (!OpenGPUBase)
		return;
	q = OGPU_Query(OGPU_OP_DWT_INV, OGPU_FMT_ARGB32);
	if (OGPU_ANSWER(q) == OGPU_NONE)
		return;
	s_ready = 1;
	rdp_progressive_set_idwt(idwt_tile, NULL);
}

void rdp_ogpu_dwt_remove(void)
{
	rdp_progressive_set_idwt(NULL, NULL);
	s_ready = 0;
}

int rdp_ogpu_dwt_ready(void)
{
	return s_ready;
}
