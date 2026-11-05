/*
 * GFAmigaRDP: known hosts; see knownhosts.h. Plain C stdio, so the host
 * tests run it too.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 */
#include "knownhosts.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

const char* rdp_known_hosts_path = RDP_KNOWN_HOSTS_FILE;

#define MAX_LINE 400

static int hexval(int c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* a "host:port" key as it is compared (review 2 N3): the host in lower
 * case (DNS names are not case-sensitive) and without a trailing dot, so
 * "Server.Example.ORG." and "server.example.org" are the same pin */
static void norm_key(char* k)
{
	char* colon = strrchr(k, ':');
	char* p;
	for (p = k; *p; p++)
		if (*p >= 'A' && *p <= 'Z')
			*p = (char)(*p - 'A' + 'a');
	if (colon && colon > k + 1 && colon[-1] == '.')
		memmove(colon - 1, colon, strlen(colon) + 1);
}

/* "host:port" for the file, normalised; 0, or -1 when it does not fit or has a space */
static int key(char* out, size_t size, const char* host, uint16_t port)
{
	int n;
	if (!host || !*host || strpbrk(host, " \t\r\n"))
		return -1;
	n = snprintf(out, size, "%s:%u", host, (unsigned)port);
	if (n <= 0 || (size_t)n >= size)
		return -1;
	norm_key(out);
	return 0;
}

/* the file is rewritten as path + ".new" and renamed over it (review 2
 * N3); a crash between taking the old one away and the rename leaves
 * only the new one, which is read then */
static int tmp_path(char* out, size_t size)
{
	int n = snprintf(out, size, "%s.new", rdp_known_hosts_path);
	return n > 0 && (size_t)n < size ? 0 : -1;
}

static FILE* open_read(void)
{
	char tmp[300];
	FILE* fh = fopen(rdp_known_hosts_path, "r");
	if (!fh && tmp_path(tmp, sizeof(tmp)) == 0)
		fh = fopen(tmp, "r");
	return fh;
}

/* a line's key and fingerprint: 1, or 0 when it is not a host line */
static int parse(char* line, char** k, uint8_t fp[32])
{
	char* sp;
	int i;
	size_t l = strlen(line);
	while (l && (line[l - 1] == '\n' || line[l - 1] == '\r' || line[l - 1] == ' '))
		line[--l] = 0;
	if (!line[0] || line[0] == '#')
		return 0;
	sp = strchr(line, ' ');
	if (!sp)
		return 0;
	*sp++ = 0;
	*k = line;
	for (i = 0; i < 32; i++)
	{
		int a, b;
		if (*sp == ':')
			sp++;
		a = hexval((unsigned char)sp[0]);
		b = a < 0 ? -1 : hexval((unsigned char)sp[1]);
		if (a < 0 || b < 0)
			return 0;
		fp[i] = (uint8_t)(a << 4 | b);
		sp += 2;
	}
	return 1;
}

int rdp_known_host_get(const char* host, uint16_t port, uint8_t fp[32])
{
	char want[300], line[MAX_LINE], *k;
	uint8_t f[32];
	int found = 0;
	FILE* fh;
	if (key(want, sizeof(want), host, port) < 0)
		return 0;
	fh = open_read();
	if (!fh)
		return 0;
	while (fgets(line, sizeof(line), fh))
		if (parse(line, &k, f) && (norm_key(k), !strcmp(k, want)))
		{
			memcpy(fp, f, 32);
			found = 1;   /* the last line for it wins */
		}
	fclose(fh);
	return found;
}

void rdp_fingerprint_text(const uint8_t fp[32], char out[96])
{
	static const char hex[] = "0123456789ABCDEF";
	int i;
	for (i = 0; i < 32; i++)
	{
		out[i * 3] = hex[fp[i] >> 4];
		out[i * 3 + 1] = hex[fp[i] & 15];
		out[i * 3 + 2] = i < 31 ? ':' : 0;
	}
}

int rdp_known_host_set(const char* host, uint16_t port, const uint8_t fp[32])
{
	char want[300], line[MAX_LINE], copy[MAX_LINE], *k, text[96], tmp[300];
	char* keep = NULL;
	size_t kept = 0, cap = 0;
	uint8_t f[32];
	FILE* fh;
	int rc = 0;
	if (key(want, sizeof(want), host, port) < 0 || tmp_path(tmp, sizeof(tmp)) < 0)
		return -1;
	/* the other lines, as they were, all of them (review 2 N3: no cap) */
	fh = open_read();
	if (fh)
	{
		while (fgets(line, sizeof(line), fh))
		{
			size_t l;
			strcpy(copy, line);
			if (parse(copy, &k, f) && (norm_key(k), !strcmp(k, want)))
				continue;
			l = strlen(line);
			if (kept + l + 2 > cap)
			{
				char* p = realloc(keep, cap = (kept + l + 2) * 2);
				if (!p)
				{
					free(keep);
					fclose(fh);
					return -1;
				}
				keep = p;
			}
			memcpy(keep + kept, line, l);
			kept += l;
			if (l && line[l - 1] != '\n')
				keep[kept++] = '\n';
		}
		fclose(fh);
	}
	fh = fopen(tmp, "w");
	if (!fh)
	{
		/* the drawer first (ENVARC:GFAmigaRDP) */
		const char* slash = strrchr(rdp_known_hosts_path, '/');
		if (slash && (size_t)(slash - rdp_known_hosts_path) < sizeof(copy))
		{
			memcpy(copy, rdp_known_hosts_path, slash - rdp_known_hosts_path);
			copy[slash - rdp_known_hosts_path] = 0;
			mkdir(copy, 0755);
			fh = fopen(tmp, "w");
		}
	}
	if (!fh)
	{
		free(keep);
		return -1;
	}
	if (!kept)
		fputs("# GFAmigaRDP known hosts: host:port and the SHA-256 of the certificate trusted for it\n", fh);
	else if (fwrite(keep, 1, kept, fh) != kept)
		rc = -1;
	rdp_fingerprint_text(fp, text);
	if (fprintf(fh, "%s %s\n", want, text) < 0)
		rc = -1;
	if (fclose(fh) != 0)
		rc = -1;
	free(keep);
	if (rc < 0)
	{
		remove(tmp);   /* the old file stays as it was */
		return -1;
	}
	/* over the old one: dos.library's Rename() will not replace a file,
	 * so when it says no the old one goes first */
	if (rename(tmp, rdp_known_hosts_path) != 0)
	{
		remove(rdp_known_hosts_path);
		if (rename(tmp, rdp_known_hosts_path) != 0)
			return -1;
	}
	return 0;
}
