/* gfopenntlm.library: NTLMv2 and CredSSP for AmigaOS 3.x.
 *
 * Written from Microsoft's published Open Specifications ([MS-NLMP],
 * [MS-CSSP]) and the RFCs only; see README.md and docs/gfopenntlm.doc.
 * MIT licensed. Copyright (c) 2026 AmiCradle Team.
 *
 * The handles are opaque. Config structures start with their Size so that
 * later versions can add fields; set Size = sizeof(struct ...) and clear
 * the structure before filling it in.
 */
#ifndef LIBRARIES_GFOPENNTLM_H
#define LIBRARIES_GFOPENNTLM_H

#if defined(__amigaos__) || defined(AMIGA) || defined(__AMIGA__) || defined(__amigaos)
#include <exec/types.h>
#else
/* a PC build of the core (the host tests) */
#include <stdint.h>
typedef uint32_t ULONG; typedef int32_t LONG; typedef uint8_t UBYTE;
typedef void *APTR; typedef const void *CONST_APTR; typedef const char *CONST_STRPTR;
#ifndef VOID
#define VOID void
#endif
#ifndef CONST
#define CONST const
#endif
#endif

#define GFOPENNTLMLIB_NAME    "gfopenntlm.library"
#define GFOPENNTLMLIB_VERSION 1

/* GN_Version(): (library version << 16) | revision */
#define GN_LIB_VERSION  1
#define GN_LIB_REVISION 0

/* Results. GNERR_CONTINUE: send the output and call again with the reply. */
#define GNERR_OK            0
#define GNERR_CONTINUE      1
#define GNERR_NOMEM        -1   /* out of memory */
#define GNERR_BADARGS      -2   /* a NULL or out-of-range argument */
#define GNERR_BUFFER       -3   /* the output buffer is too small */
#define GNERR_PROTOCOL     -4   /* the peer's message is malformed */
#define GNERR_SERVER       -5   /* the server sent a TSRequest errorCode (GNA_SERVERERROR) */
#define GNERR_PUBKEY       -6   /* the server's pubKeyAuth does not match the TLS key */
#define GNERR_INTEGRITY    -7   /* an NTLM signature does not verify */
#define GNERR_NORANDOM     -8   /* no random bytes (gfopentls.library 1 is needed) */
#define GNERR_STATE        -9   /* the call is out of order */
#define GNERR_UNSUPPORTED -10   /* the server asks for something not implemented */

/* opaque handles */
struct GNNTLM;
struct GNCredSSP;

/* GN_NTLMNew(): an NTLMv2 client. Strings are UTF-8 (plain char, as
 * C string literals are); NULL means "". */
struct GNNTLMConfig {
	ULONG        Size;          /* sizeof(struct GNNTLMConfig) */
	ULONG        Flags;         /* 0 */
	const char *User;
	const char *Domain;
	const char *Password;      /* used for the NT hash, then wiped */
	const char *Workstation;
	const char *TargetName;    /* the SPN for MsvAvTargetName, or NULL */
};

/* GN_CredSSPNew(): a CredSSP client over NTLM. */
struct GNCredSSPConfig {
	ULONG        Size;          /* sizeof(struct GNCredSSPConfig) */
	ULONG        Flags;         /* 0 */
	const char *User;
	const char *Domain;
	const char *Password;      /* kept (wiped on free) for TSPasswordCreds */
	const char *Workstation;
	const char *ServerName;    /* host name; the SPN is TERMSRV/<ServerName> */
	CONST_APTR   PublicKey;     /* the TLS certificate's SubjectPublicKey (the */
	ULONG        PublicKeyLength; /* BIT STRING's contents, unused-bits byte excluded) */
	ULONG        MaxVersion;    /* the TSRequest version to offer, 2..6; 0 = 6 */
};

/* attributes for GN_CredSSPGetAttr() and GN_NTLMGetAttr() */
#define GNA_VERSION        1    /* CredSSP: the version in use */
#define GNA_PEERVERSION    2    /* CredSSP: the server's TSRequest version */
#define GNA_SERVERERROR    3    /* CredSSP: the server's errorCode (an NTSTATUS) */
#define GNA_NTLMFLAGS      4    /* the negotiated NTLM flags */
#define GNA_NTLMPEERVERSION 5   /* the CHALLENGE's Version: major, minor, build (LE bytes 0..3) */
#define GNA_STATE          6    /* 0..3 as the exchange goes; 3 done; 9 failed */
#define GNA_LASTERROR      7    /* the GNERR_ that ended the exchange */

/* algorithms for GN_Digest() and GN_HMAC() */
#define GNALG_MD4     1         /* 16 bytes out */
#define GNALG_MD5     2         /* 16 bytes out */
#define GNALG_SHA256  3         /* 32 bytes out */

/* an output buffer of this size takes any CredSSP step */
#define GN_CREDSSP_BUFFER 8192

#endif
