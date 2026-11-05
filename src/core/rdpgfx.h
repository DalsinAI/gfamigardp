/*
 * GFAmigaRDP: the Graphics Pipeline client ([MS-RDPEGFX] 2.2.1-2.2.3,
 * 3.1.5, 3.3.5) on the "Microsoft::Windows::RDS::Graphics" dynamic
 * virtual channel. It owns the surfaces, the bitmap cache and the
 * Graphics Output Buffer (struct rdp_fb), and speaks only through a send
 * callback, so DRDYNVC (or a host test) plugs in from outside.
 *
 * H.264 is never offered: every RDPGFX 10.x capability set we send has
 * RDPGFX_CAPS_FLAG_AVC_DISABLED, 8.1 never has AVC420_ENABLED, and 10.1
 * (no flags field) is not sent. Codecs: Uncompressed and Planar decode;
 * the others are counted and skipped (the region stays as it was).
 * Plain C99. MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_RDPGFX_H
#define RDP_RDPGFX_H

#include <stddef.h>
#include <stdint.h>
#include "gfx_fb.h"
#include "zgfx.h"
#include "progressive.h"

#define RDPGFX_CHANNEL_NAME "Microsoft::Windows::RDS::Graphics"

/* 2.2.1.5 RDPGFX_HEADER cmdId */
#define RDPGFX_CMDID_WIRETOSURFACE_1 0x0001
#define RDPGFX_CMDID_WIRETOSURFACE_2 0x0002
#define RDPGFX_CMDID_DELETEENCODINGCONTEXT 0x0003
#define RDPGFX_CMDID_SOLIDFILL 0x0004
#define RDPGFX_CMDID_SURFACETOSURFACE 0x0005
#define RDPGFX_CMDID_SURFACETOCACHE 0x0006
#define RDPGFX_CMDID_CACHETOSURFACE 0x0007
#define RDPGFX_CMDID_EVICTCACHEENTRY 0x0008
#define RDPGFX_CMDID_CREATESURFACE 0x0009
#define RDPGFX_CMDID_DELETESURFACE 0x000A
#define RDPGFX_CMDID_STARTFRAME 0x000B
#define RDPGFX_CMDID_ENDFRAME 0x000C
#define RDPGFX_CMDID_FRAMEACKNOWLEDGE 0x000D
#define RDPGFX_CMDID_RESETGRAPHICS 0x000E
#define RDPGFX_CMDID_MAPSURFACETOOUTPUT 0x000F
#define RDPGFX_CMDID_CACHEIMPORTOFFER 0x0010
#define RDPGFX_CMDID_CACHEIMPORTREPLY 0x0011
#define RDPGFX_CMDID_CAPSADVERTISE 0x0012
#define RDPGFX_CMDID_CAPSCONFIRM 0x0013
#define RDPGFX_CMDID_MAPSURFACETOWINDOW 0x0015
#define RDPGFX_CMDID_QOEFRAMEACKNOWLEDGE 0x0016
#define RDPGFX_CMDID_MAPSURFACETOSCALEDOUTPUT 0x0017
#define RDPGFX_CMDID_MAPSURFACETOSCALEDWINDOW 0x0018

/* 2.2.2.1 codecId */
#define RDPGFX_CODECID_UNCOMPRESSED 0x0000
#define RDPGFX_CODECID_CAVIDEO 0x0003
#define RDPGFX_CODECID_CLEARCODEC 0x0008
#define RDPGFX_CODECID_CAPROGRESSIVE 0x0009
#define RDPGFX_CODECID_PLANAR 0x000A
#define RDPGFX_CODECID_AVC420 0x000B
#define RDPGFX_CODECID_ALPHA 0x000C
#define RDPGFX_CODECID_AVC444 0x000E
#define RDPGFX_CODECID_AVC444v2 0x000F

/* 2.2.1.4 RDPGFX_PIXELFORMAT */
#define GFX_PIXEL_FORMAT_XRGB_8888 0x20
#define GFX_PIXEL_FORMAT_ARGB_8888 0x21

/* 2.2.3 capability versions and flags */
#define RDPGFX_CAPVERSION_8 0x00080004u
#define RDPGFX_CAPVERSION_81 0x00080105u
#define RDPGFX_CAPVERSION_10 0x000A0002u
#define RDPGFX_CAPVERSION_101 0x000A0100u
#define RDPGFX_CAPVERSION_102 0x000A0200u
#define RDPGFX_CAPVERSION_103 0x000A0301u
#define RDPGFX_CAPVERSION_104 0x000A0400u
#define RDPGFX_CAPVERSION_105 0x000A0502u
#define RDPGFX_CAPVERSION_106 0x000A0600u
#define RDPGFX_CAPVERSION_107 0x000A0701u
#define RDPGFX_CAPS_FLAG_THINCLIENT 0x01u
#define RDPGFX_CAPS_FLAG_SMALL_CACHE 0x02u
#define RDPGFX_CAPS_FLAG_AVC420_ENABLED 0x10u
#define RDPGFX_CAPS_FLAG_AVC_DISABLED 0x20u
#define RDPGFX_CAPS_FLAG_AVC_THINCLIENT 0x40u
#define RDPGFX_CAPS_FLAG_SCALEDMAP_DISABLE 0x80u

#define RDP_GFX_MAX_SURFACES 16
#define RDP_GFX_CACHE_SLOTS 4096                /* 3.3.1.4 with SMALL_CACHE */
#define RDP_GFX_CACHE_BYTES (16u * 1024 * 1024)

#define RDP_GFX_E_PROTO (-1)
#define RDP_GFX_E_NOMEM (-2)
#define RDP_GFX_E_SEND (-3)

/* Sends one client-to-server RDPGFX message (not segmented, 2.2.5). */
typedef int (*rdp_gfx_send_fn)(void* user, const uint8_t* data, size_t len);

struct rdp_gfx_surface
{
	uint8_t used, fmt, mapped;
	uint16_t id, w, h;
	uint32_t ox, oy;              /* on the output buffer, when mapped */
	uint32_t* px;                 /* w x h */
	struct rdp_dirty dirty;       /* changed since the last flush */
	struct rdp_prog_surface prog; /* RemoteFX Progressive tile state */
};

struct rdp_gfx_cache_entry
{
	uint16_t w, h;
	uint32_t* px;                 /* NULL: empty slot */
};

struct rdp_gfx_stats
{
	uint32_t pdus, frames, bytes_in;
	uint32_t solid_fills, s2s, s2c, c2s;
	uint32_t uncompressed, planar;
	uint32_t skipped_codec;       /* ClearCodec, Alpha, CAVIDEO, WireToSurface2 ... */
	uint32_t avc_seen;            /* never offered, should stay 0 */
	uint32_t errors;
	uint16_t last_skipped_codec;
	uint32_t progressive, prog_errors;   /* WireToSurface2 decoded / rejected */
	struct rdp_prog_stats prog;
	uint32_t codec_count[16];     /* WireToSurface tiles by codecId (WireToSurface2: 9, Progressive) */
};

struct rdp_gfx
{
	struct rdp_mem mem;
	struct rdp_fb fb;             /* the Graphics Output Buffer, for the UI */
	rdp_gfx_send_fn send;
	void* send_user;
	struct rdp_zgfx zgfx;
	uint32_t caps_version, caps_flags;
	uint16_t offer_mask;          /* rdp_gfx_build_caps_mask; 0: every set */
	uint8_t caps_ok, in_frame;
	uint32_t frame_id, frames_decoded;
	uint8_t* scratch;             /* planar planes */
	size_t scratch_cap;
	size_t cache_bytes;
	struct rdp_gfx_surface surf[RDP_GFX_MAX_SURFACES];
	struct rdp_gfx_cache_entry* cache;   /* RDP_GFX_CACHE_SLOTS, index = slot - 1 */
	struct rdp_gfx_stats st;
};

/* w x h: the first output buffer size (Reset Graphics will change it). */
int rdp_gfx_init(struct rdp_gfx* g, const struct rdp_mem* mem, uint16_t w, uint16_t h,
                 rdp_gfx_send_fn send, void* user);
void rdp_gfx_free(struct rdp_gfx* g);

/* RDPGFX_CAPS_ADVERTISE_PDU (2.2.2.18) into buf; its length, or 0 if too small. */
size_t rdp_gfx_build_caps(uint8_t* buf, size_t size);
/* The same with only some of the sets: bit n is the n-th, newest first
 * (10.7, 10.6, 10.5, 10.4, 10.3, 10.2, 10.0, 8.1, 8.0); 0: all of them.
 * RDPProbe's GFXCAPS, to find what a server takes. */
size_t rdp_gfx_build_caps_mask(uint8_t* buf, size_t size, uint16_t mask);
/* Call once the channel is open (after the DYNVC_CREATE_RSP has gone):
 * sends the caps advertise. */
int rdp_gfx_on_open(struct rdp_gfx* g);
/* One server-to-client DVC message: RDP_SEGMENTED_DATA. */
int rdp_gfx_input(struct rdp_gfx* g, const uint8_t* data, size_t len);
/* Already decompressed RDPGFX PDUs, back to back (tests use it). */
int rdp_gfx_process_pdus(struct rdp_gfx* g, const uint8_t* p, size_t len);
/* Copies the dirty parts of mapped surfaces into fb (EndFrame does it). */
void rdp_gfx_flush(struct rdp_gfx* g);

#endif
