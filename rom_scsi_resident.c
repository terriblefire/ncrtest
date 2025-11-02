//
// NCR SCSI Test Tool - ROM resident command wrapper
// Based on ncr_dmatest resident structure
//

#include <proto/dos.h>
#include <proto/exec.h>

#include <exec/nodes.h>
#include <exec/resident.h>

/* Build date is set by Makefile */
#ifndef BUILD_DATE
#define BUILD_DATE "unknown"
#endif

#define XSTR(x) #x
#define STR(x) XSTR(x)
#define ID_STRING "ncr_scsitest 0.01 (" STR(BUILD_DATE) ")\n\r"

void* Init();
int Run();
extern int end;

int Start()
{
    return Run();
}

__section(CODE) struct Resident romtag =
{
    .rt_MatchWord   = RTC_MATCHWORD,
    .rt_MatchTag    = &romtag,
    .rt_EndSkip     = &end,
    .rt_Flags       = RTF_AFTERDOS,
    .rt_Version     = 1,
    .rt_Type        = NT_UNKNOWN,
    .rt_Pri         = 0,
    .rt_Name        = "ncr_scsitest",
    .rt_IdString    = ID_STRING,
    .rt_Init        = (APTR)Init
};
