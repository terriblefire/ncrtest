/*
 * NCR 53C710 DMA Test Tool for Amiga 4000T
 *
 * Header file with register definitions and structures
 */

#ifndef NCR_DMATEST_H
#define NCR_DMATEST_H

#include <exec/types.h>
#include <exec/resident.h>
#include <exec/memory.h>

/* Version information - BUILD_DATE is set by Makefile */
#ifndef BUILD_DATE
#define BUILD_DATE "unknown"
#endif
#define VERSION_STRING "ncrtest 0.01 (" BUILD_DATE ")"

/* NCR 53C710 register structure for big-endian access */
struct ncr710 {
	UBYTE sien;	// SCSI interrupt enable
	UBYTE sdid;	// SCSI destination ID
	UBYTE scntl1;	// SCSI control reg 1
	UBYTE scntl0;	// SCSI control reg 0
	UBYTE socl;	// SCSI Output Control Latch
	UBYTE sodl;	// SCSI Output Data Latch
	UBYTE sxfer;	// SCSI Transfer reg
	UBYTE scid;	// SCSI Chip ID
	UBYTE sbcl;	// SCSI Bus Control Lines
	UBYTE sbdl;	// SCSI Bus Data Lines		(read only)
	UBYTE sidl;	// SCSI Input Data Latch	(read only)
	UBYTE sfbr;	// SCSI First Byte Received
	UBYTE sstat2;	// SCSI Status Register 2	(read only)
	UBYTE sstat1;	// SCSI Status Register 1	(read only)
	UBYTE sstat0;	// SCSI Status Register 0	(read only)
	UBYTE dstat;	// DMA Status			(read only)
	ULONG dsa;	// Data Structure Address
	UBYTE ctest3;	// Chip Test Register 3		(read only)
	UBYTE ctest2;	// Chip Test Register 2		(read only)
	UBYTE ctest1;	// Chip Test Register 1		(read only)
	UBYTE ctest0;	// Chip Test Register 0
	UBYTE ctest7;	// Chip Test Register 7
	UBYTE ctest6;	// Chip Test Register 6
	UBYTE ctest5;	// Chip Test Register 5
	UBYTE ctest4;	// Chip Test Register 4
	ULONG temp;	// Temporary Stack
	UBYTE lcrc;	// Longitudinal Parity
	UBYTE ctest8;	// Chip Test Register 8
	UBYTE istat;	// Interrupt Status
	UBYTE dfifo;	// DMA FIFO
	ULONG dbc;	// DMA Byte Count (actually 3 bytes!)
	ULONG dnad;	// DMA Next Address
	ULONG dsp;	// DMA SCRIPTs Pointer
	ULONG dsps;	// DMA SCRIPTs Pointer Save
	ULONG scratch;	// General Purpose Scratch Pad
	UBYTE dcntl;	// DMA Control
	UBYTE dwt;	// DMA Watchdog Timer
	UBYTE dien;	// DMA Interrupt Enable
	UBYTE dmode;	// DMA Mode
	ULONG adder;	// Sum output of internal adder	(read only)
};

/* NCR 53C710 SCRIPTS instruction formats */
struct memmove_inst {
	UBYTE op;	// 11000000  Memory-to-Memory move
	UBYTE len[3];	// 24-bit length in bytes for move
	ULONG source;	// source address for move
	ULONG dest;	// dest address for move
};

struct jump_inst {
	UBYTE op;	// 10XXXMCI  X=opcode, MCI=phase
	UBYTE control;	// R0C0JDPW  R=Relative, J=Jump on True/False,
	UBYTE mask;	// XXXXXXXX  X=mask for compare
	UBYTE data;	// XXXXXXXX  X=data for compare with SFBR
	ULONG addr;	// address or offset for jump
};

struct rw_reg_inst {
	UBYTE op;	// 01XXXYYZ  X=opcode, Y=operator Z=carry
	UBYTE reg;	// 00XXXXXX  X=register addr
	UBYTE imm;	// XXXXXXXX  X=8-bit immediate
	UBYTE res;	// 00000000  reserved
	LONG res2;	// 0 - reserved
};

/* Register bit definitions */

// dstat
#define DSTATF_DFE	(1<<7)
#define DSTATF_BF	(1<<5)
#define DSTATF_ABRT	(1<<4)
#define DSTATF_SSI	(1<<3)
#define DSTATF_SIR	(1<<2)
#define DSTATF_WTD	(1<<1)
#define DSTATF_IID	(1<<0)

// istat
#define ISTATF_ABRT	(1<<7)
#define ISTATF_RST	(1<<6)
#define ISTATF_SIGP	(1<<5)
#define ISTATF_CON	(1<<3)
#define ISTATF_SIP	(1<<1)
#define ISTATF_DIP	(1<<0)

// dmode
#define DMODEF_BL1	(1<<7)
#define DMODEF_BL0	(1<<6)
#define DMODEF_FC2	(1<<5)
#define DMODEF_FC1	(1<<4)
#define DMODEF_PD	(1<<3)
#define DMODEF_FAM	(1<<2)
#define DMODEF_U0	(1<<1)
#define DMODEF_MAN	(1<<0)

// dien
#define DIENF_BF	(1<<5)
#define DIENF_ABRT	(1<<4)
#define DIENF_SSI	(1<<3)
#define DIENF_SIR	(1<<2)
#define DIENF_WTD	(1<<1)
#define DIENF_IID	(1<<0)

// dcntl
#define DCNTLF_CF1	(1<<7)
#define DCNTLF_CF0	(1<<6)
#define DCNTLF_EA	(1<<5)
#define DCNTLF_SSM	(1<<4)
#define DCNTLF_LLM	(1<<3)
#define DCNTLF_STD	(1<<2)
#define DCNTLF_FA	(1<<1)
#define DCNTLF_COM	(1<<0)

// scntl0
#define SCNTL0F_ARB1	(1<<7)
#define SCNTL0F_ARB0	(1<<6)
#define SCNTL0F_START	(1<<5)
#define SCNTL0F_WATN	(1<<4)
#define SCNTL0F_EPC	(1<<3)
#define SCNTL0F_EPG	(1<<2)
#define SCNTL0F_AAP	(1<<1)
#define SCNTL0F_TRG	(1<<0)

// scntl1
#define SCNTL1F_EXC	(1<<7)
#define SCNTL1F_ADB	(1<<6)
#define SCNTL1F_ESR	(1<<5)
#define SCNTL1F_CON	(1<<4)
#define SCNTL1F_RST	(1<<3)
#define SCNTL1F_AESP	(1<<2)
#define SCNTL1F_SND	(1<<1)
#define SCNTL1F_RCV	(1<<0)

// ctest0
#define CTEST0F_BTD	(1<<2)	// Byte-to-byte timer disable
#define CTEST0F_EAN	(1<<1)	// Enable active negation
#define CTEST0F_ERF	(1<<0)	// Enable REQ/ACK filter

// ctest7
#define CTREST7_CDIS	(1<<7)	// Disable burst bus mode

// sxfer
#define SXFERF_DHP	(1<<7)	// Disable halt on parity error

/* Hardware addresses for A4000T */
#define NCR_ADDRESS	 0x00dd0040
#define NCR_WRITE_OFFSET 0x00000080	// offset for long writes - 128 bytes

/* Write to NCR register with proper offset for longword writes */
#define WRITE_LONG(base,reg,val)	\
	*((volatile ULONG *) (((ULONG) (base)) + NCR_WRITE_OFFSET + \
			      ((ULONG)&((struct ncr710 *)0)->reg))) = (val)

/* Test parameters */
#define TEST_BUFFER_SIZE  (128*1024)   // 64KB test buffer
#define MAX_TEST_SIZE     (16*1024)   // Max DMA transfer size per test
#define MIN_TEST_SIZE     4           // Minimum DMA transfer size
#define NUM_TEST_PATTERNS 5           // Number of test patterns

/* Scatter-gather test parameters */
#define MAX_SG_SEGMENTS   8           // Maximum scatter-gather segments
#define SG_SEGMENT_SIZE   (4*1024)    // Size of each scatter-gather segment
#define SG_STRESS_ITERATIONS 1000     // Stress test iteration count

/* Test status codes */
#define TEST_SUCCESS      0
#define TEST_FAILED       1
#define TEST_TIMEOUT      2
#define TEST_DMA_ERROR    3
#define TEST_VERIFY_ERROR 4

/* Test pattern types */
#define PATTERN_ZEROS     0
#define PATTERN_ONES      1
#define PATTERN_WALKING   2
#define PATTERN_ALTERNATING 3
#define PATTERN_RANDOM    4

/* Test results structure */
struct TestResult {
	ULONG test_number;
	ULONG pattern_type;
	ULONG size;
	ULONG status;
	ULONG error_offset;
	ULONG expected_value;
	ULONG actual_value;
	ULONG duration_ticks;
};

/* Global SysBase pointer - defined in romstart.asm */
extern struct ExecBase *SysBase;

/* Function prototypes */
void kprintf(char *,...);
void dbgprintf(const char *format, ...);
void TestMain(void);
LONG DetectNCR(volatile struct ncr710 *ncr);
LONG InitNCR(volatile struct ncr710 *ncr);
LONG ResetNCR(volatile struct ncr710 *ncr);
LONG RunDMATest(volatile struct ncr710 *ncr, UBYTE *src, UBYTE *dst, ULONG size);
void FillPattern(UBYTE *buffer, ULONG size, ULONG pattern_type);
LONG VerifyBuffer(UBYTE *src, UBYTE *dst, ULONG size, struct TestResult *result);
void PrintTestResults(struct TestResult *result);

#endif /* NCR_DMATEST_H */
