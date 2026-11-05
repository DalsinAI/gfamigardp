/*
 * RemoteFX Progressive's compute through OpenGPU (stream v1.10).
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_OGPU_PROG_H
#define RDP_OGPU_PROG_H

/* Opens gfopengpu.library (shared OpenGPUBase) and, when OGPU_Query answers
 * RLGR_DEC / RFX_TILE + RFX_YCBCR, makes them Progressive's back end. */
void rdp_ogpu_prog_install(void);
void rdp_ogpu_prog_remove(void);
/* bit 0: RLGR_DEC in use; bit 1: RFX_TILE + RFX_YCBCR in use */
int rdp_ogpu_prog_ready(void);

#endif
