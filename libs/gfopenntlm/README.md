# gfopenntlm

`gfopenntlm.library`: NTLMv2 and CredSSP for AmigaOS 3.x (68020 and up), as
a shared library or a static link. It gives an RDP client Network Level
Authentication, and anything else on the Amiga NTLM.

By Team 6 / Sacredtrees for the AmiCradle Team. MIT licensed (LICENSE).

## What it does

- NTLMv2 client: NEGOTIATE, CHALLENGE, AUTHENTICATE with the MIC,
  MsvAvTargetName and the server's timestamp; extended session security,
  key exchange, GSS_WrapEx/GSS_UnwrapEx sealing and signing.
- CredSSP client, TSRequest versions 2 to 6: NTLM in negoTokens, the TLS
  public key binding in pubKeyAuth (the SHA-256 binding hash with
  clientNonce from version 5), the server's errorCode (an NTSTATUS), and the
  password credentials (TSCredentials/TSPasswordCreds) in authInfo.
- MD4, MD5, SHA-256, HMAC and RC4, in-tree, so the library has no
  dependency but gfopentls.library 1 for random numbers.

## Specifications

Clean room: written only from these published documents. No code from
another NTLM or CredSSP implementation (FreeRDP, WinPR, rdesktop, Samba,
or any other) was used.

| Specification | Used for |
|---|---|
| [MS-NLMP] NT LAN Manager (NTLM) Authentication Protocol, v20260330 | the messages, NTLMv2, the MIC, session security; section 4.2's test vectors |
| [MS-CSSP] Credential Security Support Provider Protocol, v20240423 | TSRequest, TSCredentials, TSPasswordCreds, the sequence and the TLS binding |
| RFC 1320 The MD4 Message-Digest Algorithm | MD4 and its test suite |
| RFC 1321 The MD5 Message-Digest Algorithm | MD5 and its test suite |
| RFC 2104 HMAC; RFC 2202 its MD5 test cases | HMAC-MD5 |
| FIPS 180-4 Secure Hash Standard | SHA-256 |
| RFC 6229 Test Vectors for the Stream Cipher RC4 | RC4 (the cipher as described there) and its tests |
| ITU-T X.690 (DER) | the ASN.1 encoding of TSRequest |

## Building

    STOVE=~/AmiCradle/stoves/os32-gcc16 OUT=build-amiga sh build-amiga.sh

gives `lib/gfopenntlm.library` (copy to `LIBS:`), `lib/libgfopenntlm.a`,
`include/` (libraries, clib, inline, proto, fd, sfd), `docs/gfopenntlm.doc`
and three Amiga tests. When the PC has a C compiler the build first runs
the PC tests: `sh tests/run_host_tests.sh` (MS-NLMP 4.2.4 byte for byte,
the RFC vectors, and CredSSP versions 2 to 6 against a test server).

`include/sfd/gfopenntlm_lib.sfd` is the source of the fd, clib and inline
headers and the library's entry points: after changing it run
`python3 tools/gen_headers.py`.

## Using it

    #include <proto/gfopenntlm.h>
    struct Library *GFOpenNTLMBase = OpenLibrary(GFOPENNTLMLIB_NAME, 1);

or define `GFOPENNTLM_STATIC` and link `libgfopenntlm.a`. See
`docs/gfopenntlm.doc` for the calls; GFAmigaRDP's `tools/RDPProbe` is a
complete CredSSP client over gfopentls.
