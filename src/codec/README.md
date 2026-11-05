# src/codec

The bitmap decoders, from [MS-RDPBCGR], [MS-RDPEGDI] and [MS-RDPEGFX]:

- gfx_fb.c: the Graphics Output Buffer and dirty rectangles shared by the
  connection and UI tasks; memory through struct rdp_mem (AllocVec
  MEMF_ANY on the Amiga, so Fast RAM first), lock hooks for a
  SignalSemaphore. Pixels are uint32_t 0xAARRGGBB (RECTFMT_ARGB on 68k).
- zgfx.c: RDP 8.0 bulk decompression ([MS-RDPEGFX] 2.2.5, 3.1.9.1), the
  wrapper on every server RDPGFX message; 2.5 MB history ring.
- planar.c: planar ([MS-RDPEGDI] 2.2.2.5.1, 3.1.9) with RLE, AYCoCg, colour
  loss and chroma super-sampling; and RDPGFX Uncompressed.
- later: ClearCodec ([MS-RDPEGFX] 2.2.4.1), RemoteFX Progressive
  ([MS-RDPEGFX] 2.2.4.2, GNOME Remote Desktop may need it), Alpha, interleaved
  RLE ([MS-RDPBCGR] 2.2.9.1.1.3.1.2.4) and NSCodec ([MS-RDPNSC]). No AVC/H.264.

Host tests: tests/host/test_gfx.c (the specs' own ZGFX and RLE examples).
