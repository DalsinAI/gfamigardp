/*
 * Optional OpenGPU inverse DWT for Progressive tiles (OGPU_OP_DWT_INV).
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_OGPU_DWT_H
#define RDP_OGPU_DWT_H

void rdp_ogpu_dwt_install(void);
void rdp_ogpu_dwt_remove(void);
int rdp_ogpu_dwt_ready(void);

#endif
