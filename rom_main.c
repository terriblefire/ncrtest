//
// NCR DMA Test Tool - ROM module main code
// Based on cpufreq structure
//

#include <stdint.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <exec/nodes.h>
#include <exec/resident.h>

void* Init(__reg("d0") APTR libraryBase, __reg("a0") BPTR segList, __reg("a6") struct ExecBase* SysBase);
int Run(__reg("d0") ULONG argc, __reg("a0") APTR argp, __reg("a3") BPTR segList);
int Launch(__reg("d0") ULONG argc, __reg("a0") APTR argp, __reg("a1") APTR entry, __reg("a3") BPTR seglist);

extern struct Resident romtag;
extern uint8_t ncr_dmatest_cli;
extern int end;

static __section(CODE) struct SegListTrampoline
{
    ULONG next;
    UWORD jmp;
    APTR address;
} trampoline =
{
    .next = 0,
    .jmp = 0x4ef9,
    .address = (APTR)Run
};

void* Init(__reg("d0") APTR libraryBase,
           __reg("a0") BPTR segList,
           __reg("a6") struct ExecBase* SysBase)
{
    struct DosLibrary* DOSBase = (struct DosLibrary*)OpenLibrary(DOSNAME, 36);

    if (DOSBase) {
        AddSegment("ncr_dmatest", MKBADDR(&trampoline), CMD_INTERNAL);
        CloseLibrary((struct Library*)DOSBase);
    }

    return 0;
}

static uint32_t Copy(__reg("d1") void* readhandle,
                     __reg("d2") void* buffer,
                     __reg("d3") uint32_t length,
                     __reg("a6") struct DosLibrary* DOSBase)
{
    struct ExecBase* SysBase = *(struct ExecBase**)4;
    uint8_t** p = readhandle;

    uint32_t available = (ULONG)&end - (ULONG)*p;
    if (available < length)
        length = available;

    CopyMem(*p, buffer, length);
    *p += length;

    return length;
}

static void* Alloc(__reg("d0") uint32_t size,
                   __reg("d1") uint32_t flags,
                   __reg("a6") struct ExecBase* SysBase)
{
    return AllocMem(size, flags);
}

static void Free(__reg("a1") void* memory,
                 __reg("d0") uint32_t size,
                 __reg("a6") struct ExecBase* SysBase)
{
    FreeMem(memory, size);
}

int Run(__reg("d0") ULONG argc, __reg("a0") APTR argp, __reg("a3") BPTR segList)
{
    struct ExecBase* SysBase = *((struct ExecBase**)(4L));
    struct DosLibrary* DOSBase = (struct DosLibrary*)OpenLibrary(DOSNAME, 36);

    if (!DOSBase) {
        return 1337;
    }

    {
        uint8_t* fh = &ncr_dmatest_cli;
        LONG funcs[] = { (LONG)Copy, (LONG)Alloc, (LONG)Free };
        LONG stackSize = 0;
        segList = InternalLoadSeg((BPTR)&fh, 0, funcs, &stackSize);
    }

    if (!segList) {
        CloseLibrary((struct Library*)DOSBase);
        return 1337;
    }

    int ret = Launch(argc, argp, BADDR(segList+1), segList);

    InternalUnLoadSeg(segList, Free);
    CloseLibrary((struct Library*)DOSBase);

    return ret;
}
