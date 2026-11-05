/*
 * Optional OpenGPU YUV colour convert for Progressive tiles (I420 pilot).
 * CPU YCbCr in progressive.c stays the reference; this path is used only
 * when OGPU_Query(OGPU_OP_YUV) answers and the hook is installed.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_OGPU_YUV_H
#define RDP_OGPU_YUV_H

#include <stdint.h>

/* Install/remove the Progressive YCbCr hook (no-op when gfopengpu is absent). */
void rdp_ogpu_yuv_install(void);
void rdp_ogpu_yuv_remove(void);

/* 1 when the last Query said YUV is available (for status text). */
int rdp_ogpu_yuv_ready(void);

#endif
