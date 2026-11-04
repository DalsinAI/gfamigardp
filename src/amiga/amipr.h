/*
 * amipr: GFAmigaRDP's AmigaOS 3.x platform layer, with a neutral C API.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 *
 * Callers never see an Amiga type: everything here is plain C (stdint,
 * stddef), so the header can be included next to code that has its own
 * BOOL/BYTE/WORD typedefs. The implementation talks to exec.library,
 * dos.library, timer.device, locale.library, bsdsocket.library,
 * gfopencrypto.library and gfopentls.library directly.
 *
 * Conventions: functions answer 0 for success and a negative AMIPR_E*
 * value for failure unless stated otherwise.
 */
#ifndef AMIPR_H
#define AMIPR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AMIPR_OK 0
#define AMIPR_EINVAL (-1)    /* bad argument */
#define AMIPR_ENOMEM (-2)
#define AMIPR_ENOLIB (-3)    /* a library or device could not be opened */
#define AMIPR_EDOS (-4)      /* dos.library failed; amipr_dos_error() says why */
#define AMIPR_ETIMEOUT (-5)
#define AMIPR_EBUSY (-6)     /* a lock is held elsewhere */
#define AMIPR_ENOTSUP (-7)
#define AMIPR_ETLS (-8)      /* see amipr_tls_error_text() */
#define AMIPR_EAGAIN (-9)    /* non-blocking I/O would block */
#define AMIPR_ECLOSED (-10)
#define AMIPR_ENET (-11)      /* a socket call failed; amipr_net_error() says why */
#define AMIPR_ENOTFOUND (-12) /* the host name is not known */

/* ---- process lifetime -------------------------------------------------- */
int amipr_init(void);              /* idempotent; every call below also does it lazily */
void amipr_thread_exit(void);      /* frees the calling task's cached timer request and wake signal */
const char* amipr_version(void);

/* ---- time (timer.device, locale.library) -------------------------------- */
uint64_t amipr_monotonic_us(void);    /* E-clock based, never goes back */
int64_t amipr_unix_time_us(void);     /* UTC, from the system clock and the locale offset */
int amipr_utc_offset_minutes(void);   /* local = UTC + offset (east positive) */
const char* amipr_timezone_name(void); /* the locale's country code, e.g. "deu", or "" */
void amipr_sleep_us(uint64_t us);     /* TR_ADDREQUEST on UNIT_MICROHZ; 0 just yields */
void amipr_yield(void);

/* ---- waitable objects and waiting (exec signals, WaitSelect) ------------
 * A waitable object (wobj) is a counter: signaled while it is above zero.
 * Its handle is an int in a range libnix file descriptors never reach,
 * so it can travel through code that stores "file descriptors".
 * A wobj may also be armed with a due time (and a period): it becomes
 * signaled when a waiter looks at it after that time, so no extra task
 * is needed for timers. */
#define AMIPR_WOBJ_BASE 0x40000000
int amipr_wobj_new(uint32_t initial_count);   /* handle, or < 0 */
void amipr_wobj_free(int h);
int amipr_is_wobj(int fd);
int amipr_wobj_set(int h);                    /* count = max(count, 1); wakes waiters */
int amipr_wobj_add(int h, uint32_t n, uint32_t limit, uint32_t* previous); /* semaphores */
int amipr_wobj_reset(int h);                  /* count = 0 */
int amipr_wobj_take(int h);                   /* 1 if a unit was taken, 0 if none */
int amipr_wobj_count(int h);                  /* the count (after due-time evaluation), or < 0 */
int amipr_wobj_arm(int h, uint64_t due_monotonic_us, uint64_t period_us); /* due 0: disarm */

#define AMIPR_WAIT_READ 1u
#define AMIPR_WAIT_WRITE 2u
struct amipr_wait_item
{
	int fd;            /* a wobj handle or a socket of this task's bsdsocket base */
	unsigned events;   /* AMIPR_WAIT_* asked for */
	unsigned revents;  /* AMIPR_WAIT_* that are ready, filled in */
};
/* timeout_us < 0: no timeout. Answers the number of ready items, 0 on
 * timeout, or < 0. Never consumes a wobj (use amipr_wobj_take). */
int amipr_wait(struct amipr_wait_item* items, size_t count, int64_t timeout_us);

/* ---- atomics: exec Disable()/Enable(), safe against tasks and interrupts */
int32_t amipr_atomic_add(volatile int32_t* p, int32_t v);          /* answers the new value */
int32_t amipr_atomic_cas(volatile int32_t* p, int32_t cmp, int32_t xchg); /* answers the old value */
int64_t amipr_atomic_cas64(volatile int64_t* p, int64_t cmp, int64_t xchg);

/* ---- dos.library ------------------------------------------------------- */
long amipr_dos_error(void);                       /* IoErr() */
int amipr_set_file_time(const char* path, int64_t unix_time_s);   /* SetFileDate */
int amipr_get_file_time(const char* path, int64_t* unix_time_s);  /* Examine */
int amipr_set_readonly(const char* path, int readonly);           /* SetProtection */
int amipr_is_readonly(const char* path);                          /* 1, 0, or < 0 */
int amipr_disk_space(const char* path, uint64_t* total, uint64_t* free_bytes,
                     uint32_t* bytes_per_block);                   /* Info */

struct amipr_dirent
{
	char name[108];
	int is_dir;
	uint64_t size;
	int64_t mtime_unix;
	int readonly;
};
struct amipr_dir;                                               /* Lock + Examine/ExNext */
struct amipr_dir* amipr_dir_open(const char* path);
int amipr_dir_next(struct amipr_dir* d, struct amipr_dirent* e); /* 1 entry, 0 end, < 0 */
void amipr_dir_close(struct amipr_dir* d);

int amipr_path_join(char* dir, size_t size, const char* part);  /* AddPart */
int amipr_path_absolute(const char* path, char* buf, size_t size); /* Lock + NameFromLock */
const char* amipr_path_file_part(const char* path);              /* FilePart */
int amipr_path_exists(const char* path);                         /* 1 file, 2 dir, 0 none */
int amipr_mkdir(const char* path);                               /* CreateDir */
int amipr_delete(const char* path);                              /* DeleteFile */
int amipr_rename(const char* from, const char* to);              /* Rename */

/* Environment: the process's local variables first (like a Windows
 * process environment), then global ENV: ones. */
int amipr_getenv(const char* name, char* buf, size_t size);   /* length, or < 0 when unset */
int amipr_setenv(const char* name, const char* value);        /* local variable */
int amipr_unsetenv(const char* name);
typedef int (*amipr_env_cb)(void* user, const char* name, const char* value);
int amipr_env_foreach(amipr_env_cb cb, void* user);           /* local variables */

#define AMIPR_DIR_HOME 0     /* $HOME, else PROGDIR: */
#define AMIPR_DIR_CONFIG 1   /* ENVARC: */
#define AMIPR_DIR_TEMP 2     /* T: */
#define AMIPR_DIR_DATA 3     /* PROGDIR: */
#define AMIPR_DIR_CACHE 4    /* T: */
int amipr_known_dir(int which, char* buf, size_t size);
int amipr_user_name(char* buf, size_t size);                  /* $USER, $USERNAME, else "Amiga" */
int amipr_host_name(char* buf, size_t size);                  /* bsdsocket gethostname, else $HOSTNAME */
uint32_t amipr_cpu_count(void);                               /* 1 */
uint64_t amipr_total_memory(void);                            /* AvailMem(MEMF_TOTAL) */

/* ---- bsdsocket.library ---------------------------------------------------
 * Each task opens its own bsdsocket.library base (a base belongs to the
 * task that opened it); sockets are bsdsocket descriptors of that base and
 * are used from that task. amipr_wait() takes them as items. */
int amipr_net_open(void);                 /* this task's base; AMIPR_ENOLIB with no TCP/IP stack */
void amipr_net_close(void);
void* amipr_socket_base(void);            /* this task's base, or NULL */
int amipr_inet_pton4(const char* s, uint8_t out[4]);     /* inet_aton; 1 ok, 0 not an address */
int amipr_inet_ntop4(const uint8_t in[4], char* buf, size_t size); /* Inet_NtoA */
/* resolve and connect, waiting at most timeout_us (< 0: the stack's own
 * timeout); answers a non-blocking socket or < 0 */
int amipr_tcp_connect(const char* host, uint16_t port, int64_t timeout_us);
int amipr_tcp_send(int s, const void* buf, int len);  /* bytes, AMIPR_EAGAIN, or < 0 */
int amipr_tcp_recv(int s, void* buf, int len);        /* bytes, 0 closed, AMIPR_EAGAIN, or < 0 */
void amipr_tcp_close(int s);
int amipr_net_error(void);                            /* Errno() of the last call */

/* ---- memory (exec AllocVec) --------------------------------------------- */
void* amipr_alloc(size_t size);
void amipr_free(void* p);

/* ---- crypto (gfopencrypto.library; SHA-1 is our own) -------------------- */
int amipr_random(void* buf, size_t len);   /* gfopentls.library OT_Random() */

struct amipr_hash
{
	int alg;                 /* AMIPR_SHA* */
	uint8_t state[64];
	uint8_t block[128];
	size_t used;
	uint64_t total;
};
#define AMIPR_SHA1 1
#define AMIPR_SHA256 2
#define AMIPR_SHA384 3
#define AMIPR_SHA512 4
int amipr_hash_init(struct amipr_hash* h, int alg);
int amipr_hash_update(struct amipr_hash* h, const void* data, size_t len);
int amipr_hash_final(struct amipr_hash* h, uint8_t* out, size_t out_size); /* digest length */
size_t amipr_hash_size(int alg);
size_t amipr_hash_block_size(int alg);

#define AMIPR_AES_ECB_ENCRYPT 3
#define AMIPR_AES_CBC_ENCRYPT 1
#define AMIPR_AES_CBC_DECRYPT 2
int amipr_aes(uint8_t* buf, size_t len, const uint8_t* key, size_t keylen, const uint8_t iv[16],
              int mode);
/* out = in^e mod n (big-endian, n_len bytes) */
int amipr_rsa_public(uint8_t* out, const uint8_t* in, const uint8_t* n, size_t n_len,
                     const uint8_t* e, size_t e_len);

/* ---- TLS client (gfopentls.library) ------------------------------------- */
struct amipr_tls;
/* I/O below the TLS layer: answer bytes moved (> 0), 0 at end of stream,
 * AMIPR_EAGAIN, or another negative value for an error. */
typedef int (*amipr_tls_io)(void* user, int write, void* buf, int len);
struct amipr_tls* amipr_tls_new(const char* sni_host, amipr_tls_io io, void* user);
void amipr_tls_free(struct amipr_tls* t);
int amipr_tls_handshake(struct amipr_tls* t);       /* 0, AMIPR_EAGAIN, or AMIPR_ETLS */
int amipr_tls_read(struct amipr_tls* t, void* buf, int len);
int amipr_tls_write(struct amipr_tls* t, const void* buf, int len);
int amipr_tls_pending(struct amipr_tls* t);
int amipr_tls_close(struct amipr_tls* t);
/* the server's leaf certificate, DER, valid until amipr_tls_free */
int amipr_tls_peer_certificate(struct amipr_tls* t, const uint8_t** der, size_t* len);
const char* amipr_tls_error_text(struct amipr_tls* t);
const char* amipr_tls_cipher(struct amipr_tls* t);
int amipr_tls_protocol(struct amipr_tls* t);         /* 0x0303 TLS 1.2, 0x0304 TLS 1.3 */
const char* amipr_tls_backend_name(void);

#ifdef __cplusplus
}
#endif
#endif
