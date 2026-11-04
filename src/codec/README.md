# src/codec

The bitmap decoders, from [MS-RDPBCGR] and [MS-RDPEGDI]:

- interleaved RLE ([MS-RDPBCGR] 2.2.9.1.1.3.1.2.4, [MS-RDPEGDI] 3.1.9)
  for 8, 15, 16 and 24 bits a pixel;
- planar ([MS-RDPEGDI] 2.2.2.5.1, 3.1.9.2) for 32 bits;
- later: NSCodec ([MS-RDPNSC]) and the orders' caches.

Each decoder writes rows into a caller's buffer; src/ui/desktop.c draws them
(docs/DESIGN.md, "Drawing"). Nothing here yet.
