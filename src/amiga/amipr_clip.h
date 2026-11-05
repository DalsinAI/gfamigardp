/*
 * amipr: Amiga clipboard.device unit 0 as IFF FTXT/CHRS text.
 * Autodocs: devices/clipboard.h; IFF FORM FTXT (EA IFF 85).
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef AMIPR_CLIP_H
#define AMIPR_CLIP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Open clipboard.device PRIMARY_CLIP. Idempotent. 0 or AMIPR_E*. */
int amipr_clip_init(void);
void amipr_clip_shutdown(void);

/* Current CBD_CURRENTREADID, or 0 if the device is not open. */
int32_t amipr_clip_read_id(void);

/* Read FTXT/CHRS as UTF-8 into dst (NUL-terminated). Amiga CHRS is treated
 * with rdp_amiga_to_utf8. 0 when text was read (possibly empty), AMIPR_ENOTSUP
 * when the clip is not FTXT, or another AMIPR_E*. */
int amipr_clip_get_text(char* dst, size_t size);

/* Write UTF-8 as FTXT/CHRS (Latin-1 best-effort). 0 or AMIPR_E*. */
int amipr_clip_set_text(const char* utf8);

#ifdef __cplusplus
}
#endif

#endif
