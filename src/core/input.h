/*
 * GFAmigaRDP: client input ([MS-RDPBCGR] 2.2.8.1.2 fast-path Input Event
 * PDU). Built as bytes here; the connection sends them without waiting on
 * the socket (rdp_conn_input_*). Spec only; no FreeRDP/WinPR.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_INPUT_H
#define RDP_INPUT_H

#include <stddef.h>
#include <stdint.h>

/* Fast-path event codes (eventHeader bits 5..7) */
#define RDP_FP_EVENT_SCANCODE  0
#define RDP_FP_EVENT_MOUSE     1
#define RDP_FP_EVENT_MOUSEX    2
#define RDP_FP_EVENT_SYNC      3
#define RDP_FP_EVENT_UNICODE   4

/* Keyboard eventFlags */
#define RDP_FP_KBD_RELEASE     0x01
#define RDP_FP_KBD_EXTENDED    0x02
#define RDP_FP_KBD_EXTENDED1   0x04

/* TS_POINTER_EVENT pointerFlags (also used in fast-path mouse) */
#define RDP_PTR_WHEEL_NEG      0x0100
#define RDP_PTR_WHEEL          0x0200
#define RDP_PTR_HWHEEL         0x0400
#define RDP_PTR_MOVE           0x0800
#define RDP_PTR_BUTTON1        0x1000
#define RDP_PTR_BUTTON2        0x2000
#define RDP_PTR_BUTTON3        0x4000
#define RDP_PTR_DOWN           0x8000

/* One fast-path event in a builder buffer (header + payload). */
struct rdp_fp_event
{
	uint8_t code;       /* RDP_FP_EVENT_* */
	uint8_t flags;      /* kbd flags, or 0 for mouse */
	uint8_t scancode;   /* set-1 scancode, or 0 */
	uint16_t ptr_flags; /* mouse */
	uint16_t x, y;      /* mouse: remote desktop coords */
	uint16_t unicode;   /* unicode event */
};

/* Map an Amiga rawkey (Code without IECODE_UP_PREFIX) to a PC set-1
 * scancode. Returns 1 and fills *scan / *ext (1 = EXTENDED), or 0 if the
 * key has no mapping (Left/Right Amiga stay local for menus). */
int rdp_rawkey_to_scan(uint16_t raw, uint8_t* scan, int* ext);

/* Build a TS_FP_INPUT_PDU (ENCRYPTION_LEVEL_NONE: no MAC) into out.
 * Returns the PDU length, or < 0 if out is too small / n is 0 or > 15. */
int rdp_fp_input_build(uint8_t* out, size_t out_size, const struct rdp_fp_event* ev, int n);

/* Convenience: one scancode down or up. */
int rdp_fp_input_key(uint8_t* out, size_t out_size, uint8_t scancode, int extended, int release);
/* One mouse event (flags + x/y). */
int rdp_fp_input_mouse(uint8_t* out, size_t out_size, uint16_t ptr_flags, uint16_t x, uint16_t y);
/* Ctrl-Alt-Del: six events (Ctrl, Alt, Del down then up). */
int rdp_fp_input_cad(uint8_t* out, size_t out_size);

#endif
