/* gfopenntlm.library's proto header. MIT, Copyright (c) 2026 AmiCradle Team.
 * Define GFOPENNTLM_STATIC to call the functions of libgfopenntlm.a
 * directly instead of the library. */
#ifndef PROTO_GFOPENNTLM_H
#define PROTO_GFOPENNTLM_H

#include <libraries/gfopenntlm.h>

#if defined(GFOPENNTLM_STATIC)
#include <clib/gfopenntlm_protos.h>
#else
#include <exec/types.h>
#ifndef __NOLIBBASE__
extern struct Library *GFOpenNTLMBase;
#endif
#if defined(__GNUC__) && !defined(GFOPENNTLM_NO_INLINE)
#include <inline/gfopenntlm.h>
#else
#include <clib/gfopenntlm_protos.h>
#endif
#endif

#endif
