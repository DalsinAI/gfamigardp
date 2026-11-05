# GFAmigaRDP design

A Remote Desktop client for AmigaOS 3.2 on a 68040 with no GPU, written
from the Microsoft Open Specifications (CLEAN-ROOM.md). This file is the
plan; each layer names the sections it follows.

## Layers

```
 src/ui      connection window, session window, menus, favourites   (OpenLayout UI)
   |
 src/core    connection sequence -> input / output PDUs -> channels
   |            X.224 | MCS/GCC | security | licensing | capabilities
 src/crypto  X.509, MD4, RC4, HMAC-MD5, NTLMv2, CredSSP encoding
 src/codec   interleaved RLE, planar, (NSCodec), caches
   |
 src/amiga   amipr: timer, signals, dos, bsdsocket, gfopentls, gfopencrypto
```

Each layer only calls downwards. src/core and src/crypto are plain C99
with no Amiga includes (they also build on a PC for the tests); only
src/amiga and src/ui include Amiga headers.

## Transport

- TCP through bsdsocket.library v4. The library base is per task: amipr
  keeps a list of tasks' resources keyed by FindTask (no `__thread`).
  Sockets are non-blocking; amipr_wait waits on sockets and amipr wait
  objects together with WaitSelect, so the UI's signals can wake it.
- TLS through gfopentls.library, behind amipr_tls (handshake, read, write,
  the peer certificate). The transport gives it an I/O callback over the
  socket, and waits only for the direction TLS is blocked on (read or
  write), with one deadline for the whole handshake.
- The certificate is checked (OTV_FULL: a trusted CA, the dates, the host
  name) or must be the one the user trusted before for that host:port
  (ENVARC:GFAmigaRDP/KnownHosts, SHA-256, trust on first use as SSH;
  src/core/knownhosts.c). A certificate that fails is shown (subject,
  name match, fingerprint) and the user decides; a changed one is
  warned about. The Client Info PDU (which holds the password under TLS)
  is never sent to an unverified server, and the server cannot pick a
  protocol that was not offered (no silent downgrade). The default offer
  is NLA only (HYBRID | HYBRID_EX); TLS alone is a choice in the window.
- The network runs on the window's task: while connecting, every wait
  also wakes on the window's signal (amipr_set_abort), so the window is
  redrawn and Cancel/Esc/Ctrl-C stop the connection. The session window
  waits on the socket and its IDCMP together (amipr_wait_signals) and
  reads PDUs without blocking: a half PDU waits in the buffer.
- Everything above reads TPKT packets ([MS-RDPBCGR] 2.2.1.1, T.123) or
  fast-path PDUs (2.2.9.1.2: the first byte's low two bits are 0).

The connection sequence (TCP, TLS, NLA, MCS, licensing) runs on its own CreateNewProc task (`src/amiga/conntask.c`), talking to the UI only through exec message ports (`CT_CMD_*` / `CT_EVT_*`). The UI stays live for Cancel and the certificate dialog; after `CT_EVT_READY` the live `rdp_conn` is handed to the session window.

## Connection ([MS-RDPBCGR] 1.3.1.1)

1. **Connection initiation** (2.2.1.1, 2.2.1.2): X.224 CR with the
   mstshash cookie and RDP_NEG_REQ (NLA by default; TLS when chosen), the CC with
   RDP_NEG_RSP or RDP_NEG_FAILURE. *Done.*
2. **Security upgrade**: TLS straight after the CC (5.4.5.1); for NLA,
   CredSSP over it (below). *TLS done; NLA next.*
3. **Basic settings exchange** (2.2.1.3, 2.2.1.4): MCS Connect Initial
   (T.125 BER) carrying GCC Conference Create Request (PER) with the client
   core, security, network and cluster data; the response's server data.
4. **Channel connection** (2.2.1.5-2.2.1.8): Erect Domain, Attach User,
   Channel Join for the user, I/O and each static channel.
5. **Security commencement** (2.2.1.10): only for standard RDP security;
   not offered at first (TLS/NLA only).
6. **Secure settings exchange** (2.2.1.11): Client Info PDU (user, domain,
   flags, the extended info).
7. **Licensing** ([MS-RDPELE]): handle the server's "valid client" and
   error alerts; the full exchange (new licence, upgrade) later.
8. **Capabilities exchange** (2.2.1.13): read Demand Active, answer
   Confirm Active with general, bitmap, order, pointer, input, brush,
   glyph cache, offscreen cache, virtual channel, sound and fast-path
   capability sets.
9. **Connection finalization** (2.2.1.14-2.2.1.22): Synchronize, Control
   Cooperate, Control Request Control, Font List; wait for Font Map.

## Security

- **TLS**: gfopentls.library. Its chain check result, the host name check
  (src/crypto/x509.c, RFC 6125) and a SHA-256 fingerprint shown to the
  user, remembered per favourite (trust on first use, like a known_hosts).
- **NLA** ([MS-CSSP]): TSRequest (DER) with SPNEGO-wrapped NTLM
  ([MS-NLMP]): NEGOTIATE, CHALLENGE, AUTHENTICATE with NTLMv2, the MIC,
  sealing of pubKeyAuth (the server's SubjectPublicKey, version 5 and up
  with the client nonce hash), then TSCredentials.
  - From gfopencrypto.library: SHA-256, HMAC-SHA256, random bytes.
  - Written here, clean room from the RFCs: MD4 (RFC 1320), HMAC-MD5
    (RFC 2104), RC4 (vectors from RFC 6229), the NTLMv2 key derivation
    ([MS-NLMP] 3.3.2, 3.4.5).
- Passwords are never written to disk; favourites keep host, user, domain
  and settings only.

## After connecting

- **Input**: fast-path input (2.2.8.1.2): scancodes from a keymap table
  (rawkey to PC set 1 scancode, extended keys), Unicode events for
  characters the table lacks, mouse moves and buttons, the wheel. Toolbar
  "Ctrl-Alt-Del" sends the three scancodes.
- **Output**: fast-path and slow-path updates (2.2.9.1):
  1. bitmap updates (2.2.9.1.1.3.1.2) uncompressed, then interleaved RLE
     and planar (src/codec);
  2. palette updates for 8-bit sessions;
  3. pointer updates (system, colour, cached, new pointer) to an Intuition
     pointer;
  4. later: drawing orders and their caches ([MS-RDPEGDI]): DstBlt,
     PatBlt, ScrBlt, OpaqueRect, MemBlt with the bitmap cache, glyphs.
- **Channels**: drdynvc ([MS-RDPEDYC]) is the Dynamic Virtual Channel
  manager (src/core/drdynvc.c, vchan.c) on the DRDYNVC static channel
  (CS_NET + join + session routing). It negotiates capabilities, accepts
  create/close, and reassembles data; Microsoft::Windows::RDS::Graphics
  is registered for RDPGFX ([MS-RDPEGFX], src/core/rdpgfx.c): with
  rdp_conn.gfx_enable set (RDPProbe: EARLY 0100) the client sends caps
  8.0-10.7 that never offer AVC (AVC_DISABLED on every 10.x, no 10.1),
  decompresses RDP8 bulk data (src/codec/zgfx.c), keeps surfaces, the
  bitmap cache and the Graphics Output Buffer (src/codec/gfx_fb.c, all
  AllocVec(MEMF_ANY), so Fast RAM), decodes Uncompressed and Planar,
  and acknowledges each frame; ClearCodec, Progressive and Alpha are
  counted and skipped for now. With the flag off the channel is accepted
  and counted only. The UI takes the dirty rectangles with
  rdp_fb_take_dirty and blits them (not wired yet). Later:
  cliprdr ([MS-RDPECLIP]), rdpsnd ([MS-RDPEA]) through AHI, maybe drive
  redirection ([MS-RDPEFS]).

## Drawing

The desktop is a buffer in the session's colour depth; updates are
decoded into it and the changed rectangles drawn:

1. **gfopengpu stream** when the GFOpenGPU driver is present (the Open
   Apps path for a 68040 with no GPU: the stream batches rectangles).
2. **OpenGfx / WritePixelArray** (CyberGraphX or P96) on an RTG screen:
   24-bit RGB rows (RECTFMT_RGB), converted from the session depth.
3. **WriteChunkyPixels** on a native screen: pens from a colour cube
   with ordered dithering (what the demo does today), or the 8-bit
   session's palette mapped to pens.

The 68040 has no time to spare: 16-bit sessions at 800x600 by default,
dirty rectangles only, no scaling. Full screen opens a screen of the
session's size (the ModeID chosen with BestModeID).

## UI

GFOpenLayout's UI (libolui, linked statically) laid out from markup and
CSS (src/ui/markup.c), themed by GFOpenGadTools (ENV:GFOpenGadTools/Look
names SYS:Prefs/Presets/Themes/<Look>.theme).

- **Connection window**: computer, user name, domain, password;
  resolution, colours, security; the favourites list with Save and
  Delete; Connect and Cancel; a status line. Underlined letters and
  GadTools menus with Amiga-key shortcuts (Project, Edit, Help).
- **Session window**: a toolbar strip (olui_amiga_strip: Disconnect,
  Ctrl-Alt-Del, Full screen, Keys..., and "user on host"), the desktop,
  and a status strip (state on the left, mode and security on the right).
- **Open Apps rules**: works on a 68040 without an FPU or GPU; the
  Classic theme looks like a stock 3.2 GadTools window (passthrough);
  window sizes come from the screen font; nothing busy-waits.
- **Demo**: `DEMO` fills sample favourites and a Connect opens a session
  with a test pattern; `DEMO SESSION` opens it straight away.

Known gap: olui has no access keys, so the program handles VANILLAKEY
itself when no text field has the focus (src/ui/connect_win.c).

## Roadmap

| Phase | What | Risk |
|-------|------|------|
| 0 (done) | skeleton: amipr, TCP, X.224, TLS upgrade, the two windows, demo mode, RDPProbe, X224Test | - |
| 1 | MCS/GCC basic settings, channel joins, Client Info, licensing's short path, Demand/Confirm Active, finalization: to the first update PDU against Windows with TLS only (NLA off on the server) | **High**: BER/PER encoding of GCC, every field length; Windows silently drops a bad Connect Initial |
| 2 | output: uncompressed then interleaved RLE and planar bitmaps, palette, pointer; dirty-rect drawing via WritePixelArray/WriteChunkyPixels | **High**: speed on a 68040 (RLE decode and 16-to-native conversion in C first, then assembly where profiling says) |
| 3 | input: fast-path keyboard (rawkey to scancode) and mouse | Medium: keymaps other than US/UK |
| 4 | NLA: MD4, RC4, HMAC-MD5, NTLMv2, SPNEGO, CredSSP v6 | **High**: byte-exact NTLMv2 and the pubKeyAuth binding; needs test vectors from [MS-NLMP] 4.2 |
| 5 | drawing orders and caches ([MS-RDPEGDI]); gfopengpu stream | Medium: order field encodings; cache sizes in Amiga RAM |
| 6 | full screen, reconnect, certificate trust UI, favourites polish | Low |
| 7 | channels: cliprdr, rdpsnd via AHI | Medium: AHI timing on a 68040 |

Other risks:
- GNOME Remote Desktop ends the session straight after licensing (Deactivate
  All, then Disconnect Provider Ultimatum rn-user-requested) unless the client
  data offers RNS_UD_CS_SUPPORT_DYNVC_GFX_PROTOCOL, a 32 bpp session and the
  drdynvc channel, all three (measured with RDPProbe, docs/LAB.txt). It has
  no bitmap-update path for older clients, so for it the order above changes:
  drdynvc ([MS-RDPEDYC]) and RDPGFX ([MS-RDPEGFX]: uncompressed, planar,
  then progressive/AVC as far as a 68040 allows) before phase 2's bitmaps.
- gfopentls.library's TLS 1.2 cipher suites must meet what Windows
  servers offer (ECDHE-RSA-AES-GCM); checked with RDPProbe in the lab.
- TLS handshakes with RSA-2048/3072 server keys are slow on a 68040; time
  them early.
- Memory: an 800x600x16 desktop buffer is ~940 KB plus caches; Fast RAM
  needed.
- No emulator here runs Intuition, so UI work is checked with the PC
  preview (tools/preview) and on the lab machine.
