/*
 * GFAmigaRDP: UTF-8 to UTF-16LE for the strings RDP carries (the Client
 * Info PDU's domain, user name and password). The same decoding as
 * gfopenntlm's (NLA's NTLM and TSPasswordCreds), so both layers send the
 * same code units for the same text: a byte that does not start a valid
 * sequence, or a sequence cut short, is U+FFFD; code points above U+FFFF
 * are surrogate pairs. OpenLayout's fields hold UTF-8.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_UTF16_H
#define RDP_UTF16_H

#include <stddef.h>
#include <stdint.h>

/* the UTF-16LE length of s in bytes, without a terminator */
size_t rdp_utf16_len(const char* s);
/* s as UTF-16LE into out (at most size bytes, whole code units only);
 * the bytes written, without a terminator */
size_t rdp_utf16_put(const char* s, uint8_t* out, size_t size);
/* the next code point of a UTF-8 string, advancing *p (U+FFFD for bad input) */
uint32_t rdp_utf8_next(const uint8_t** p);

#endif
