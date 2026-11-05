/*
 * GFAmigaRDP: UTF-8 to UTF-16LE for the strings RDP carries (the Client
 * Info PDU's domain, user name and password). The same decoding as
 * gfopenntlm's (NLA's NTLM and TSPasswordCreds), so both layers send the
 * same code units for the same text: a byte that does not start a valid
 * sequence, or a sequence cut short, is U+FFFD; code points above U+FFFF
 * are surrogate pairs. OpenLayout's fields hold UTF-8.
 *
 * The Amiga console and Shell (and a Favourites file typed by hand) are
 * Latin-1: convert at those entry points with rdp_latin1_to_utf8 /
 * rdp_amiga_to_utf8 before anything that reads UTF-8 (review L1).
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

/* 1 when s is valid UTF-8 (overlong and surrogates rejected); empty is ok */
int rdp_utf8_ok(const char* s);
/* Latin-1 src as UTF-8 into dst (size bytes, NUL-terminated). Bytes written
 * excluding the NUL, or 0 if size is 0. */
size_t rdp_latin1_to_utf8(char* dst, size_t size, const char* src);
/* Amiga text (Shell, console, a hand-edited Favourites line): if src is
 * already valid UTF-8 keep it, else treat every byte as Latin-1. Whole
 * UTF-8 characters only; never cuts a sequence. Bytes written excluding NUL. */
size_t rdp_amiga_to_utf8(char* dst, size_t size, const char* src);
/* Copy src into dst (size bytes) as UTF-8, cutting only on a character
 * boundary (review L2). */
void rdp_copy_utf8(char* dst, size_t size, const char* src);

#endif
