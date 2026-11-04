/* gfopenntlm.library 1.0: NTLMv2 ([MS-NLMP]) and CredSSP ([MS-CSSP]) for
 * AmigaOS 3.x. docs/gfopenntlm.doc is the contract; the calls are the C
 * functions of src/gn_api.c behind the entry points of gn_entries.h.
 * MIT licensed. Copyright (c) 2026 AmiCradle Team. */
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <clib/gfopenntlm_protos.h>
#include "../src/gn_internal.h"

#define REG(r, decl) register decl __asm(#r)

struct GNLibBase {
	struct Library lib;
	BPTR seglist;
};

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;

int start(void) { return -1; }

static const char lib_name[] = GFOPENNTLMLIB_NAME;
static const char lib_id[] =
	"gfopenntlm.library 1.0 (04.11.2026) NTLMv2 and CredSSP, AmiCradle Team\r\n";
static const char lib_ver[] __attribute__((used)) =
	"$VER: gfopenntlm.library 1.0 (04.11.2026) NTLMv2 and CredSSP, AmiCradle Team";

static struct Library *lib_init(REG(d0, struct GNLibBase *base), REG(a0, BPTR seglist),
                                REG(a6, struct ExecBase *sys));
static struct Library *lib_open(REG(a6, struct GNLibBase *base));
static BPTR lib_close(REG(a6, struct GNLibBase *base));
static BPTR lib_expunge(REG(a6, struct GNLibBase *base));
static ULONG lib_null(void);

#include "gn_entries.h"

static const APTR lib_vectors[] = {
	(APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
#include "gn_vectors.h"
	(APTR)-1
};

static const struct {
	ULONG size;
	const APTR *vectors;
	APTR data;
	APTR init;
} lib_inittable = {
	sizeof(struct GNLibBase), lib_vectors, NULL, (APTR)lib_init
};

const struct Resident lib_romtag = {
	RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
	RTF_AUTOINIT, GN_LIB_VERSION, NT_LIBRARY, 0,
	(char *)lib_name, (char *)lib_id, (APTR)&lib_inittable
};

static struct Library *lib_init(REG(d0, struct GNLibBase *base), REG(a0, BPTR seglist),
                                REG(a6, struct ExecBase *sys))
{
	SysBase = sys;
	base->seglist = seglist;
	base->lib.lib_Revision = GN_LIB_REVISION;
	DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 37);
	if (!DOSBase) {
		FreeMem((UBYTE *)base - base->lib.lib_NegSize,
		        base->lib.lib_NegSize + base->lib.lib_PosSize);
		return NULL;
	}
	return &base->lib;
}

static struct Library *lib_open(REG(a6, struct GNLibBase *base))
{
	base->lib.lib_OpenCnt++;
	base->lib.lib_Flags &= ~LIBF_DELEXP;
	return &base->lib;
}

static BPTR lib_close(REG(a6, struct GNLibBase *base))
{
	base->lib.lib_OpenCnt--;
	if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
		return lib_expunge(base);
	return 0;
}

static BPTR lib_expunge(REG(a6, struct GNLibBase *base))
{
	BPTR seglist;
	if (base->lib.lib_OpenCnt) {
		base->lib.lib_Flags |= LIBF_DELEXP;
		return 0;
	}
	seglist = base->seglist;
	Remove(&base->lib.lib_Node);
	gn_sys_cleanup();
	CloseLibrary((struct Library *)DOSBase);
	FreeMem((UBYTE *)base - base->lib.lib_NegSize,
	        base->lib.lib_NegSize + base->lib.lib_PosSize);
	return seglist;
}

static ULONG lib_null(void) { return 0; }
