# Clean room

GFAmigaRDP is written from published specifications and the Amiga autodocs
only. No code, structure, names or comments are taken from FreeRDP, WinPR,
rdesktop or any other RDP implementation, and nobody working on it reads their
sources while doing so.

## Allowed sources

- Microsoft Open Specifications: [MS-RDPBCGR], [MS-RDPELE], [MS-CSSP], [MS-NLMP],
  [MS-RDPEGDI], [MS-RDPNSC], [MS-RDPEFS], [MS-RDPECLIP], [MS-RDPEA], [MS-RDPERP]
  (as needed), [MS-ERREF] for error codes.
- ITU-T T.123 (TPKT), X.224, T.125 (MCS) and X.680/X.690 (ASN.1 BER/PER/DER);
  T.124 (GCC) as quoted in [MS-RDPBCGR].
- IETF RFCs: 1006 (TPKT), 1320 (MD4), 1321 (MD5), 2104 and 2202 (HMAC and
  its vectors), 4120 (Kerberos, later), 4178 (SPNEGO), 5246 and 8446 (TLS),
  5280 (X.509), 5929 (channel bindings), 6125 (host names), 6229 (RC4
  vectors); FIPS 180-4 (SHA-256).
- The AmigaOS 3.2 autodocs and includes (OpenNDK), the bsdsocket.library
  documentation, AHI's developer documentation, and The Open SDK's own
  documentation (GFOpenLayout, GFOpenGadTools, GFOpenTLS, GFOpenCrypto).
- Packet captures of our own sessions, read against the specifications.

## gfopenntlm

libs/gfopenntlm (gfopenntlm.library: NTLMv2 and CredSSP for NLA) follows the
same rules: [MS-NLMP] for NTLM, [MS-CSSP] for CredSSP, [MS-ERREF] for the
codes, and the RFCs above for MD4, MD5, HMAC, RC4 and SHA-256. Its tests
use only the vectors printed in [MS-NLMP] 4.2 and in those RFCs; [MS-CSSP]
has none, so its CredSSP test server is written from [MS-CSSP] 3.1.5 too.
No code from FreeRDP, WinPR, Samba, any other NTLM or CredSSP
implementation, or any other crypto library was used. The library's
shape (romtag, sfd, the generated headers) follows GFOpenTLS, our own MIT
work.

## Not allowed

- The source, headers, comments, test data or commit history of FreeRDP,
  WinPR, rdesktop, xrdp, or any other RDP client or server; for gfopenntlm
  also of Samba or any other NTLM, SPNEGO or CredSSP implementation.
- Their documentation where it describes their internal structure.

## Our own earlier work

The Amiga layer (src/amiga, "amipr") and the GFOpenTLS bridge were written by
Team 6 under MIT for an earlier port; they contain only Amiga and GFOpenTLS
code (no code from that port's upstream) and were checked again for this
repository. The earlier port itself is not part of this repository.

## Checking

Each file names the specification sections it implements. Reviews compare a
change against those sections; a change that cannot point to its source is
not taken.
