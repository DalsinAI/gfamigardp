/*
 * GFAmigaRDP: the servers the user chose to trust (trust on first use),
 * as SSH's known hosts: ENVARC:GFAmigaRDP/KnownHosts, one per line,
 *   host:port SHA-256-of-the-certificate-in-hex
 * An RDP server's certificate is mostly self-signed, so the TLS layer
 * cannot check it against a CA; its fingerprint, once the user has seen
 * it and said "trust", is pinned for the next connections (amipr_tls_new),
 * and a different certificate later is refused with a warning (review H1).
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#ifndef RDP_KNOWNHOSTS_H
#define RDP_KNOWNHOSTS_H

#include <stddef.h>
#include <stdint.h>

#define RDP_KNOWN_HOSTS_FILE "ENVARC:GFAmigaRDP/KnownHosts"

/* the file; tests set another */
extern const char* rdp_known_hosts_path;

/* 1 and fp filled when host:port is known, 0 when not */
int rdp_known_host_get(const char* host, uint16_t port, uint8_t fp[32]);
/* adds or replaces host:port; 0, or < 0 when the file cannot be written */
int rdp_known_host_set(const char* host, uint16_t port, const uint8_t fp[32]);
/* "AB:CD:..." (95 characters and a NUL) */
void rdp_fingerprint_text(const uint8_t fp[32], char out[96]);

#endif
