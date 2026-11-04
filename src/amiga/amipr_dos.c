/*
 * amipr: dos.library - files, directories, paths, environment.
 * MIT, Copyright (c) 2026 AmiCradle Team. Written by Sacredtrees (Team 6).
 *
 * File dates on AmigaOS are DateStamps in local time (days since
 * 1978-01-01, minutes, ticks of 1/50 s); we convert from and to Unix
 * seconds (UTC) with the locale offset. Protection bits are "deny" bits:
 * FIBF_WRITE and FIBF_DELETE set means the object cannot be written or
 * deleted, which is what read-only means here.
 */
#include "amipr_internal.h"

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/var.h>
#include <dos/record.h>
#include <string.h>

long amipr_dos_error(void)
{
	return IoErr();
}

static void unix_to_stamp(int64_t t, struct DateStamp* ds)
{
	int64_t local = t - AMIPR_EPOCH_DIFF + (int64_t)amipr_utc_offset_minutes() * 60;
	if (local < 0)
		local = 0;
	ds->ds_Days = (LONG)(local / 86400);
	ds->ds_Minute = (LONG)((local % 86400) / 60);
	ds->ds_Tick = (LONG)((local % 60) * TICKS_PER_SECOND);
}

static int64_t stamp_to_unix(const struct DateStamp* ds)
{
	int64_t local = (int64_t)ds->ds_Days * 86400 + (int64_t)ds->ds_Minute * 60 +
	                ds->ds_Tick / TICKS_PER_SECOND;
	return local + AMIPR_EPOCH_DIFF - (int64_t)amipr_utc_offset_minutes() * 60;
}

int amipr_set_file_time(const char* path, int64_t unix_time_s)
{
	struct DateStamp ds;
	if (!path)
		return AMIPR_EINVAL;
	unix_to_stamp(unix_time_s, &ds);
	return SetFileDate((CONST_STRPTR)path, &ds) ? AMIPR_OK : AMIPR_EDOS;
}

/* Examine an object by name into a DOS-allocated FileInfoBlock */
static struct FileInfoBlock* examine_path(const char* path)
{
	struct FileInfoBlock* fib;
	BPTR lock = Lock((CONST_STRPTR)path, SHARED_LOCK);
	if (!lock)
		return NULL;
	fib = AllocDosObject(DOS_FIB, NULL);
	if (fib && !Examine(lock, fib))
	{
		FreeDosObject(DOS_FIB, fib);
		fib = NULL;
	}
	UnLock(lock);
	return fib;
}

int amipr_get_file_time(const char* path, int64_t* unix_time_s)
{
	struct FileInfoBlock* fib;
	if (!path || !unix_time_s)
		return AMIPR_EINVAL;
	fib = examine_path(path);
	if (!fib)
		return AMIPR_EDOS;
	*unix_time_s = stamp_to_unix(&fib->fib_Date);
	FreeDosObject(DOS_FIB, fib);
	return AMIPR_OK;
}

/* record locks take 32-bit offsets and lengths: clamp the "whole file"
 * ranges callers like to ask for */
#define AMIPR_DENY_WRITE (FIBF_WRITE | FIBF_DELETE)

int amipr_is_readonly(const char* path)
{
	struct FileInfoBlock* fib = path ? examine_path(path) : NULL;
	int ro;
	if (!fib)
		return AMIPR_EDOS;
	ro = (fib->fib_Protection & AMIPR_DENY_WRITE) == AMIPR_DENY_WRITE;
	FreeDosObject(DOS_FIB, fib);
	return ro;
}

int amipr_set_readonly(const char* path, int readonly)
{
	struct FileInfoBlock* fib = path ? examine_path(path) : NULL;
	LONG prot;
	if (!fib)
		return AMIPR_EDOS;
	prot = fib->fib_Protection;
	FreeDosObject(DOS_FIB, fib);
	prot = readonly ? (prot | AMIPR_DENY_WRITE) : (prot & ~AMIPR_DENY_WRITE);
	return SetProtection((CONST_STRPTR)path, prot) ? AMIPR_OK : AMIPR_EDOS;
}

int amipr_disk_space(const char* path, uint64_t* total, uint64_t* free_bytes,
                     uint32_t* bytes_per_block)
{
	struct InfoData* id;
	BPTR lock;
	int rc = AMIPR_EDOS;

	if (!path)
		return AMIPR_EINVAL;
	lock = Lock((CONST_STRPTR)path, SHARED_LOCK);
	if (!lock)
		return AMIPR_EDOS;
	id = AllocVec(sizeof(*id), MEMF_ANY | MEMF_CLEAR); /* longword aligned */
	if (id && Info(lock, id))
	{
		uint64_t bpb = (uint64_t)id->id_BytesPerBlock;
		if (total)
			*total = (uint64_t)id->id_NumBlocks * bpb;
		if (free_bytes)
			*free_bytes = (uint64_t)(id->id_NumBlocks - id->id_NumBlocksUsed) * bpb;
		if (bytes_per_block)
			*bytes_per_block = (uint32_t)bpb;
		rc = AMIPR_OK;
	}
	FreeVec(id);
	UnLock(lock);
	return rc;
}

struct amipr_dir
{
	BPTR lock;
	struct FileInfoBlock* fib;
};

struct amipr_dir* amipr_dir_open(const char* path)
{
	struct amipr_dir* d;

	if (!path)
		return NULL;
	d = AllocVec(sizeof(*d), MEMF_ANY | MEMF_CLEAR);
	if (!d)
		return NULL;
	d->lock = Lock((CONST_STRPTR)path, SHARED_LOCK);
	d->fib = d->lock ? AllocDosObject(DOS_FIB, NULL) : NULL;
	/* Examine() fills in the directory itself; ExNext() then walks it */
	if (!d->fib || !Examine(d->lock, d->fib) || d->fib->fib_DirEntryType <= 0)
	{
		amipr_dir_close(d);
		return NULL;
	}
	return d;
}

int amipr_dir_next(struct amipr_dir* d, struct amipr_dirent* e)
{
	struct FileInfoBlock* fib;
	if (!d || !e)
		return AMIPR_EINVAL;
	fib = d->fib;
	if (!ExNext(d->lock, fib))
		return IoErr() == ERROR_NO_MORE_ENTRIES ? 0 : AMIPR_EDOS;
	strncpy(e->name, (const char*)fib->fib_FileName, sizeof(e->name) - 1);
	e->name[sizeof(e->name) - 1] = '\0';
	e->is_dir = fib->fib_DirEntryType > 0 && fib->fib_DirEntryType != ST_SOFTLINK;
	e->size = (uint64_t)(ULONG)fib->fib_Size;
	e->mtime_unix = stamp_to_unix(&fib->fib_Date);
	e->readonly = (fib->fib_Protection & AMIPR_DENY_WRITE) == AMIPR_DENY_WRITE;
	return 1;
}

void amipr_dir_close(struct amipr_dir* d)
{
	if (!d)
		return;
	if (d->fib)
		FreeDosObject(DOS_FIB, d->fib);
	if (d->lock)
		UnLock(d->lock);
	FreeVec(d);
}

int amipr_path_join(char* dir, size_t size, const char* part)
{
	if (!dir || !part || size == 0)
		return AMIPR_EINVAL;
	return AddPart((STRPTR)dir, (CONST_STRPTR)part, (ULONG)size) ? AMIPR_OK : AMIPR_EINVAL;
}

const char* amipr_path_file_part(const char* path)
{
	return path ? (const char*)FilePart((CONST_STRPTR)path) : NULL;
}

int amipr_path_absolute(const char* path, char* buf, size_t size)
{
	BPTR lock;
	if (!path || !buf || size == 0)
		return AMIPR_EINVAL;
	lock = Lock((CONST_STRPTR)path, SHARED_LOCK);
	if (lock)
	{
		LONG ok = NameFromLock(lock, (STRPTR)buf, (LONG)size);
		UnLock(lock);
		return ok ? AMIPR_OK : AMIPR_EDOS;
	}
	else
	{
		/* not there (yet): resolve the directory part, keep the name */
		size_t n = strlen(path);
		char* tmp = AllocVec(n + 1, MEMF_ANY);
		const char* name = (const char*)FilePart((CONST_STRPTR)path);
		STRPTR cut;
		int rc = AMIPR_EDOS;
		if (!tmp)
			return AMIPR_ENOMEM;
		memcpy(tmp, path, n + 1);
		cut = PathPart((STRPTR)tmp);
		*cut = '\0';
		lock = Lock((CONST_STRPTR)tmp, SHARED_LOCK); /* "" is the current directory */
		if (lock)
		{
			if (NameFromLock(lock, (STRPTR)buf, (LONG)size) &&
			    AddPart((STRPTR)buf, (CONST_STRPTR)name, (ULONG)size))
				rc = AMIPR_OK;
			UnLock(lock);
		}
		FreeVec(tmp);
		return rc;
	}
}

int amipr_path_exists(const char* path)
{
	struct FileInfoBlock* fib = path ? examine_path(path) : NULL;
	int r;
	if (!fib)
		return 0;
	r = fib->fib_DirEntryType > 0 ? 2 : 1;
	FreeDosObject(DOS_FIB, fib);
	return r;
}

int amipr_mkdir(const char* path)
{
	BPTR lock = path ? CreateDir((CONST_STRPTR)path) : 0;
	if (!lock)
		return AMIPR_EDOS;
	UnLock(lock);
	return AMIPR_OK;
}

int amipr_delete(const char* path)
{
	return path && DeleteFile((CONST_STRPTR)path) ? AMIPR_OK : AMIPR_EDOS;
}

int amipr_rename(const char* from, const char* to)
{
	return from && to && Rename((CONST_STRPTR)from, (CONST_STRPTR)to) ? AMIPR_OK : AMIPR_EDOS;
}

/* ---- environment ---- */

int amipr_getenv(const char* name, char* buf, size_t size)
{
	char one[2];
	LONG n;
	if (!name)
		return AMIPR_EINVAL;
	if (!buf || size == 0)
	{
		buf = one;
		size = sizeof(one);
	}
	/* flags 0: local variable first, then global ENV:; GVF_BINARY_VAR
	 * keeps a value with newlines whole */
	n = GetVar((CONST_STRPTR)name, (STRPTR)buf, (LONG)size, GVF_BINARY_VAR);
	if (n < 0)
		return AMIPR_EINVAL;
	/* GetVar truncates; IoErr() then holds the full length */
	if ((size_t)n >= size - 1)
	{
		LONG full = IoErr();
		if (full > n)
			n = full;
	}
	return (int)n;
}

int amipr_setenv(const char* name, const char* value)
{
	if (!name || !value)
		return AMIPR_EINVAL;
	return SetVar((CONST_STRPTR)name, (CONST_STRPTR)value, -1, GVF_LOCAL_ONLY) ? AMIPR_OK
	                                                                            : AMIPR_EDOS;
}

int amipr_unsetenv(const char* name)
{
	if (!name)
		return AMIPR_EINVAL;
	DeleteVar((CONST_STRPTR)name, GVF_LOCAL_ONLY);
	return AMIPR_OK;
}

int amipr_env_foreach(amipr_env_cb cb, void* user)
{
	struct Process* pr = (struct Process*)FindTask(NULL);
	struct LocalVar* lv;
	char small[256];

	if (!cb || pr->pr_Task.tc_Node.ln_Type != NT_PROCESS)
		return AMIPR_EINVAL;
	for (lv = (struct LocalVar*)pr->pr_LocalVars.mlh_Head; lv->lv_Node.ln_Succ;
	     lv = (struct LocalVar*)lv->lv_Node.ln_Succ)
	{
		char* v = small;
		int rc;
		if (lv->lv_Node.ln_Type != LV_VAR || (lv->lv_Flags & LVF_IGNORE))
			continue;
		if (lv->lv_Len + 1 > sizeof(small))
		{
			v = AllocVec(lv->lv_Len + 1, MEMF_ANY);
			if (!v)
				return AMIPR_ENOMEM;
		}
		CopyMem(lv->lv_Value, v, lv->lv_Len);
		v[lv->lv_Len] = '\0';
		rc = cb(user, (const char*)lv->lv_Node.ln_Name, v);
		if (v != small)
			FreeVec(v);
		if (rc)
			break;
	}
	return AMIPR_OK;
}

static int copy_str(char* buf, size_t size, const char* s)
{
	size_t n = strlen(s);
	if (!buf || n + 1 > size)
		return AMIPR_EINVAL;
	memcpy(buf, s, n + 1);
	return AMIPR_OK;
}

int amipr_known_dir(int which, char* buf, size_t size)
{
	switch (which)
	{
		case AMIPR_DIR_HOME:
			if (amipr_getenv("HOME", buf, size) > 0)
				return AMIPR_OK;
			return copy_str(buf, size, "PROGDIR:");
		case AMIPR_DIR_CONFIG:
			return copy_str(buf, size, "ENVARC:");
		case AMIPR_DIR_TEMP:
		case AMIPR_DIR_CACHE:
			return copy_str(buf, size, "T:");
		case AMIPR_DIR_DATA:
			return copy_str(buf, size, "PROGDIR:");
		default:
			return AMIPR_EINVAL;
	}
}

int amipr_user_name(char* buf, size_t size)
{
	if (amipr_getenv("USER", buf, size) > 0 || amipr_getenv("USERNAME", buf, size) > 0)
		return AMIPR_OK;
	return copy_str(buf, size, "Amiga");
}

uint32_t amipr_cpu_count(void)
{
	return 1;
}

uint64_t amipr_total_memory(void)
{
	return (uint64_t)AvailMem(MEMF_TOTAL);
}
