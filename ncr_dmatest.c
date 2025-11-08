/*
 * NCR 53C710 DMA Test Tool - Main DMA Test Functions
 */

#include "ncr_dmatest.h"
#include <stdio.h>
#include <stdlib.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <hardware/intbits.h>
#include <stdbool.h>

/* External functions */
extern LONG InitNCR(volatile struct ncr710 *ncr);
extern LONG CheckNCRStatus(volatile struct ncr710 *ncr, const char *context);

/* Global buffers for cleanup on CTRL-C */
static UBYTE *g_chip_buf1 = NULL;
static UBYTE *g_chip_buf2 = NULL;
static UBYTE *g_mbfast_buf1 = NULL;
static UBYTE *g_mbfast_buf2 = NULL;
static UBYTE *g_cpufastl_buf1 = NULL;
static UBYTE *g_cpufastl_buf2 = NULL;
static UBYTE *g_cpufastu_buf1 = NULL;
static UBYTE *g_cpufastu_buf2 = NULL;
static BOOL g_cleanup_done = FALSE;

/* SCRIPTS buffer - allocated in FAST memory */
static UBYTE *g_scripts_buf = NULL;

/* Memory region definitions */
#define MB_FAST_START    0x07000000UL
#define MB_FAST_END      0x07FFFFFFUL
#define CPU_FASTL_START  0x08000000UL
#define CPU_FASTL_END    0x0FFFFFFFUL
#define CPU_FASTU_START  0x10000000UL
#define CPU_FASTU_END    0x18000000UL
#define ALLOC_STEP       (64*1024)  // 64KB increment

/* Simple pseudo-random number generator for test patterns */
static ULONG random_seed = 0x12345678;

/*
 * Allocate memory in a specific address range using AllocAbs()
 * Searches the range in 64KB increments until successful
 * Returns NULL if no memory available in range
 */
static UBYTE *AllocInRange(ULONG start, ULONG end, ULONG size, const char *region_name)
{
	UBYTE *mem;
	ULONG addr;
	
	dbgprintf("  Searching for %ld bytes in %s region (0x%08lx-0x%08lx)...\n",
	       size, region_name, start, end);

	// Search the range in ALLOC_STEP increments
	for (addr = start; addr <= (end - size); addr += ALLOC_STEP) {
		mem = (UBYTE *)AllocAbs(size, (APTR)addr);
		if (mem) {
			dbgprintf("    Allocated at 0x%08lx\n", (ULONG)mem);
			return mem;
		}
	}

	dbgprintf("    Failed to allocate in %s region\n", region_name);
	return NULL;
}

/*
 * Cleanup function for CTRL-C or normal exit
 */
static void CleanupBuffers(void)
{
	if (g_cleanup_done)
		return;

	dbgprintf("\nCleaning up buffers...\n");

	if (g_chip_buf1) {
		FreeMem(g_chip_buf1, TEST_BUFFER_SIZE);
		g_chip_buf1 = NULL;
	}
	if (g_chip_buf2) {
		FreeMem(g_chip_buf2, TEST_BUFFER_SIZE);
		g_chip_buf2 = NULL;
	}
	if (g_mbfast_buf1) {
		FreeMem(g_mbfast_buf1, TEST_BUFFER_SIZE);
		g_mbfast_buf1 = NULL;
	}
	if (g_mbfast_buf2) {
		FreeMem(g_mbfast_buf2, TEST_BUFFER_SIZE);
		g_mbfast_buf2 = NULL;
	}
	if (g_cpufastl_buf1) {
		FreeMem(g_cpufastl_buf1, TEST_BUFFER_SIZE);
		g_cpufastl_buf1 = NULL;
	}
	if (g_cpufastl_buf2) {
		FreeMem(g_cpufastl_buf2, TEST_BUFFER_SIZE);
		g_cpufastl_buf2 = NULL;
	}
	if (g_cpufastu_buf1) {
		FreeMem(g_cpufastu_buf1, TEST_BUFFER_SIZE);
		g_cpufastu_buf1 = NULL;
	}
	if (g_cpufastu_buf2) {
		FreeMem(g_cpufastu_buf2, TEST_BUFFER_SIZE);
		g_cpufastu_buf2 = NULL;
	}
	if (g_scripts_buf) {
		FreeMem(g_scripts_buf, 256);  // Small buffer for SCRIPTS
		g_scripts_buf = NULL;
	}

	g_cleanup_done = TRUE;
}

static ULONG GetRandom(void)
{
	random_seed = random_seed * 1103515245 + 12345;
	return random_seed;
}

/*
 * Fill a buffer with a test pattern
 */
void FillPattern(UBYTE *buffer, ULONG size, ULONG pattern_type)
{
	ULONG i;

	switch (pattern_type) {
	case PATTERN_ZEROS:
		for (i = 0; i < size; i++)
			buffer[i] = 0x00;
		break;

	case PATTERN_ONES:
		for (i = 0; i < size; i++)
			buffer[i] = 0xFF;
		break;

	case PATTERN_WALKING:
		// Walking ones pattern
		for (i = 0; i < size; i++)
			buffer[i] = (1 << (i & 7));
		break;

	case PATTERN_ALTERNATING:
		// Alternating 0x55/0xAA pattern
		for (i = 0; i < size; i++)
			buffer[i] = (i & 1) ? 0xAA : 0x55;
		break;

	case PATTERN_RANDOM:
		// Pseudo-random pattern
		for (i = 0; i < size; i++) {
			if ((i & 3) == 0)
				random_seed = GetRandom();
			buffer[i] = (random_seed >> ((i & 3) * 8)) & 0xFF;
		}
		break;
	}

	CacheClearU();
}

/*
 * Verify that destination buffer matches source buffer
 */
LONG VerifyBuffer(UBYTE *src, UBYTE *dst, ULONG size, struct TestResult *result)
{
	ULONG i;

	CacheClearU();

	for (i = 0; i < size; i++) {
		if (src[i] != dst[i]) {
			result->error_offset = i;
			result->expected_value = src[i];
			result->actual_value = dst[i];
			return TEST_VERIFY_ERROR;
		}
	}

	return TEST_SUCCESS;
}

/*
 * Build a simple SCRIPTS program to perform memory-to-memory DMA
 * Returns the address of the script
 * NOTE: Uses pre-allocated FAST memory buffer (g_scripts_buf)
 */
static ULONG* BuildDMAScript(UBYTE *src, UBYTE *dst, ULONG size)
{
	struct {
		struct memmove_inst move;
		struct jump_inst    done;
	} *script = (void *)g_scripts_buf;

	if (!g_scripts_buf) {
		dbgprintf("ERROR: SCRIPTS buffer not allocated!\n");
		return NULL;
	}

	// Memory-to-memory move instruction
	script->move.op = 0xC0;  // Memory move opcode
	script->move.len[0] = (size >> 16) & 0xFF;
	script->move.len[1] = (size >> 8) & 0xFF;
	script->move.len[2] = size & 0xFF;
	script->move.source = (ULONG)src;
	script->move.dest = (ULONG)dst;

	// INT instruction to signal completion (opcode 0x98, interrupt on the fly)
	script->done.op = 0x98;
	script->done.control = 0x08;  // Interrupt always
	script->done.mask = 0x00;
	script->done.data = 0x00;
	script->done.addr = 0xDEADBEEF;  // Value that will be in DSPS

	return (ULONG*)script;
}

/*
 * Build a scatter-gather SCRIPTS program with multiple Memory Move instructions
 * Gathers data from multiple source buffers into one contiguous destination
 * Returns the address of the script
 */
static ULONG* BuildScatterGatherScript(UBYTE **sources, UBYTE *dest,
                                        ULONG *sizes, ULONG num_segments)
{
	struct memmove_inst *moves;
	struct jump_inst *done;
	ULONG i;
	ULONG dest_offset = 0;

	if (!g_scripts_buf) {
		dbgprintf("ERROR: SCRIPTS buffer not allocated!\n");
		return NULL;
	}

	if (num_segments > MAX_SG_SEGMENTS) {
		dbgprintf("ERROR: Too many scatter-gather segments (%ld > %ld)\n",
		       num_segments, (ULONG)MAX_SG_SEGMENTS);
		return NULL;
	}

	// Build array of Memory Move instructions
	moves = (struct memmove_inst *)g_scripts_buf;

	for (i = 0; i < num_segments; i++) {
		moves[i].op = 0xC0;  // Memory move opcode
		moves[i].len[0] = (sizes[i] >> 16) & 0xFF;
		moves[i].len[1] = (sizes[i] >> 8) & 0xFF;
		moves[i].len[2] = sizes[i] & 0xFF;
		moves[i].source = (ULONG)sources[i];
		moves[i].dest = (ULONG)(dest + dest_offset);

		dest_offset += sizes[i];
	}

	// Add interrupt instruction after all moves
	done = (struct jump_inst *)&moves[num_segments];
	done->op = 0x98;           // Interrupt opcode
	done->control = 0x08;      // Interrupt always
	done->mask = 0x00;
	done->data = 0x00;
	done->addr = 0xCAFEBABE;   // Different magic value for SG

	return (ULONG*)moves;
}

/*
 * Execute a DMA transfer using the NCR chip
 * Returns: TEST_SUCCESS on success, error code on failure
 */
LONG RunDMATest(volatile struct ncr710 *ncr, UBYTE *src, UBYTE *dst, ULONG size)
{
	ULONG *script;
	ULONG timeout;
	UBYTE istat;

	// Build the SCRIPTS program
	script = BuildDMAScript(src, dst, size);

	// Flush caches before DMA to ensure data/script visibility
	CacheClearU();

	// Clear any pending interrupts
	(void)ncr->istat;
	(void)ncr->dstat;
	(void)ncr->sstat0;

	// Load the script address into DSP to start execution
	WRITE_LONG(ncr, dsp, (ULONG)script);

	// Wait for completion or timeout
	for (timeout = 0; timeout < 100000; timeout++) {
		istat = ncr->istat;

		// Check for DMA interrupt
		if (istat & ISTATF_DIP) {
			UBYTE dstat = ncr->dstat;

			// Check for script interrupt (our completion signal)
			if (dstat & DSTATF_SIR) {
				// Success - script completed
				if (ncr->dsps == 0xDEADBEEF) {
					return TEST_SUCCESS;
				}
			}

			// Check for errors
			if (CheckNCRStatus(ncr, "DMA") < 0) {
				return TEST_DMA_ERROR;
			}
		}

		// Small delay
		if ((timeout & 0xFF) == 0) {
			// Every 256 iterations, give other tasks a chance
			// In a ROM module we can't call Wait(), so just spin
		}
	}

	dbgprintf("ERROR: DMA timeout\n");
	dbgprintf("  ISTAT: 0x%02lx\n", (ULONG)ncr->istat);
	dbgprintf("  DSTAT: 0x%02lx\n", (ULONG)ncr->dstat);
	dbgprintf("  DSP:   0x%08lx\n", ncr->dsp);

	return TEST_TIMEOUT;
}

/*
 * Execute a scatter-gather DMA transfer using the NCR chip
 * Multiple source buffers are gathered into one destination buffer
 * Returns: TEST_SUCCESS on success, error code on failure
 */
static LONG RunScatterGatherTest(volatile struct ncr710 *ncr, UBYTE **sources,
                                  UBYTE *dest, ULONG *sizes, ULONG num_segments)
{
	ULONG *script;
	ULONG timeout;
	UBYTE istat;

	// Build the scatter-gather SCRIPTS program
	script = BuildScatterGatherScript(sources, dest, sizes, num_segments);
	if (!script)
		return TEST_DMA_ERROR;

	// Flush caches before DMA
	CacheClearU();

	// Clear any pending interrupts
	(void)ncr->istat;
	(void)ncr->dstat;
	(void)ncr->sstat0;

	// Load the script address into DSP to start execution
	WRITE_LONG(ncr, dsp, (ULONG)script);

	// Wait for completion or timeout
	for (timeout = 0; timeout < 100000; timeout++) {
		istat = ncr->istat;

		// Check for DMA interrupt
		if (istat & ISTATF_DIP) {
			UBYTE dstat = ncr->dstat;

			// Check for script interrupt (our completion signal)
			if (dstat & DSTATF_SIR) {
				// Success - script completed
				if (ncr->dsps == 0xCAFEBABE) {
					return TEST_SUCCESS;
				}
			}

			// Check for errors
			if (CheckNCRStatus(ncr, "SG DMA") < 0) {
				return TEST_DMA_ERROR;
			}
		}

		// Small delay
		if ((timeout & 0xFF) == 0) {
			// Every 256 iterations, give other tasks a chance
		}
	}

	dbgprintf("ERROR: Scatter-gather DMA timeout\n");
	dbgprintf("  ISTAT: 0x%02lx\n", (ULONG)ncr->istat);
	dbgprintf("  DSTAT: 0x%02lx\n", (ULONG)ncr->dstat);
	dbgprintf("  DSP:   0x%08lx\n", ncr->dsp);

	return TEST_TIMEOUT;
}

/*
 * Print test result summary (only prints failures)
 */
void PrintTestResults(struct TestResult *result)
{
	const char *pattern_names[] = {
		"ZEROS",
		"ONES",
		"WALKING",
		"ALTERNATING",
		"RANDOM"
	};

	const char *status_names[] = {
		"SUCCESS",
		"FAILED",
		"TIMEOUT",
		"DMA_ERROR",
		"VERIFY_ERROR"
	};

	// Only print if test failed
	if (result->status != TEST_SUCCESS) {
		dbgprintf("  FAILED Test #%ld: Pattern=%s Size=%ld Status=%s",
		        result->test_number,
		        pattern_names[result->pattern_type],
		        result->size,
		        status_names[result->status]);

		if (result->status == TEST_VERIFY_ERROR) {
			dbgprintf("\n    ERROR at offset 0x%lx: Expected=0x%02lx Actual=0x%02lx",
			        result->error_offset,
			        result->expected_value,
			        result->actual_value);
		}

	}
}

/*
 * Run a comprehensive DMA test between two memory regions
 */
LONG RunComprehensiveTest(volatile struct ncr710 *ncr,
                          UBYTE *src_base, UBYTE *dst_base,
                          ULONG buffer_size)
{
	struct TestResult result;
	ULONG test_num = 0;
	ULONG pattern, size;
	LONG status;
	ULONG passed = 0, failed = 0;

	/*dbgprintf("\n=== Starting Comprehensive DMA Tests ===\n");
	dbgprintf("Running tests");*/

	// Test various sizes and patterns
	for (size = MIN_TEST_SIZE; size <= buffer_size && size <= MAX_TEST_SIZE; size *= 2) {
		for (pattern = 0; pattern < NUM_TEST_PATTERNS; pattern++) {

			test_num++;

			// Initialize result structure
			result.test_number = test_num;
			result.pattern_type = pattern;
			result.size = size;
			result.status = TEST_FAILED;
			result.error_offset = 0;
			result.expected_value = 0;
			result.actual_value = 0;

			// Fill source buffer with pattern
			FillPattern(src_base, size, pattern);

			// Clear destination buffer
			FillPattern(dst_base, size, PATTERN_ZEROS);

			// Run DMA transfer
			status = RunDMATest(ncr, src_base, dst_base, size);

			if (status == TEST_SUCCESS) {
				// Verify the transfer
				status = VerifyBuffer(src_base, dst_base, size, &result);
			}

			result.status = status;

			// Print progress indicator (dot for success)
			if (status == TEST_SUCCESS) {
				passed++;
			} else {
				// Print newline before error details
				PrintTestResults(&result);
				failed++;
				// For now, continue with other tests even if one fails
			}
		}
	}

	if (failed > 0)
	{
		dbgprintf("\n=== Test Summary ===\n");
		dbgprintf("Total tests: %ld\n", test_num);
		dbgprintf("Passed:      %ld\n", passed);
		dbgprintf("Failed:      %ld\n", failed);
	}

	return (failed == 0) ? 0 : -1;
}

/*
 * Test DMA transfer from one buffer to another
 */
static void TestDMATransfer(volatile struct ncr710 *ncr,
                             UBYTE *src_buf, const char *src_name,
                             UBYTE *dst_buf, const char *dst_name)
{
	if (!src_buf || !dst_buf) {
		dbgprintf("*** Skipping: %s -> %s (buffer not available) ***\n",
		       src_name, dst_name);
		return;
	}

	dbgprintf("*** Test: %s -> %s ***", src_name, dst_name);
	if (RunComprehensiveTest(ncr, src_buf, dst_buf, TEST_BUFFER_SIZE) == 0)
	{
		dbgprintf(" PASSED ***\n");
	}
	else
	{
		dbgprintf(" FAILED ***\n");
	}
}

/*
 * Memory buffer descriptor
 */
struct MemoryBuffer {
	UBYTE **buf;
	const char *name;
};

/*
 * Test scatter-gather DMA operations
 * Gathers data from multiple memory regions into one destination
 */
static void TestScatterGather(volatile struct ncr710 *ncr, int verbosity)
{
	UBYTE *sources[MAX_SG_SEGMENTS];
	ULONG sizes[MAX_SG_SEGMENTS];
	UBYTE *gather_dest;
	UBYTE *verify_buf;
	ULONG num_segments;
	ULONG i, j;
	LONG status;
	ULONG total_size = 0;
	ULONG offset = 0;
	BOOL all_passed = TRUE;

	if (verbosity > 1) dbgprintf("\n=== Scatter-Gather DMA Tests ===\n"); 

	// Allocate destination buffer for gathered data (in CPU_FASTL)
	gather_dest = AllocMem(MAX_SG_SEGMENTS * SG_SEGMENT_SIZE, MEMF_FAST | MEMF_CLEAR);
	if (!gather_dest) {
		dbgprintf("ERROR: Could not allocate gather destination buffer\n");
		return;
	}
	if (verbosity > 1) dbgprintf("Gather destination: 0x%08lx\n", (ULONG)gather_dest);

	// Allocate verification buffer
	verify_buf = AllocMem(MAX_SG_SEGMENTS * SG_SEGMENT_SIZE, MEMF_FAST | MEMF_CLEAR);
	if (!verify_buf) {
		dbgprintf("ERROR: Could not allocate verification buffer\n");
		FreeMem(gather_dest, MAX_SG_SEGMENTS * SG_SEGMENT_SIZE);
		return;
	}

	// Test 1: Gather from different memory regions
	if (verbosity > 1) dbgprintf("\n*** Test 1: Gather from multiple memory regions ***\n");

	num_segments = 0;

	// Segment 0: From CHIP RAM (if available)
	if (g_chip_buf1) {
		sources[num_segments] = g_chip_buf1;
		sizes[num_segments] = SG_SEGMENT_SIZE;
		FillPattern(sources[num_segments], sizes[num_segments], PATTERN_WALKING);
		if (verbosity > 2) {
			dbgprintf("  Segment %ld: CHIP RAM     0x%08lx -> 0x%08lx (%ld bytes)\n",
		       num_segments, (ULONG)sources[num_segments],
		       (ULONG)(gather_dest + total_size), sizes[num_segments]);
			}
		total_size += sizes[num_segments];
		num_segments++;
	}

	// Segment 1: From MB_FAST (if available)
	if (g_mbfast_buf1) {
		sources[num_segments] = g_mbfast_buf1;
		sizes[num_segments] = SG_SEGMENT_SIZE;
		FillPattern(sources[num_segments], sizes[num_segments], PATTERN_ALTERNATING);
		if (verbosity > 2) {
			dbgprintf("  Segment %ld: MB_FAST      0x%08lx -> 0x%08lx (%ld bytes)\n",
		       num_segments, (ULONG)sources[num_segments],
		       (ULONG)(gather_dest + total_size), sizes[num_segments]);
			}
		total_size += sizes[num_segments];
		num_segments++;
	}

	// Segment 2: From CPU_FASTL (if available)
	if (g_cpufastl_buf1) {
		sources[num_segments] = g_cpufastl_buf1;
		sizes[num_segments] = SG_SEGMENT_SIZE;
		FillPattern(sources[num_segments], sizes[num_segments], PATTERN_ONES);
		if (verbosity > 2) {
			dbgprintf("  Segment %ld: CPU_FASTL    0x%08lx -> 0x%08lx (%ld bytes)\n",
		       num_segments, (ULONG)sources[num_segments],
		       (ULONG)(gather_dest + total_size), sizes[num_segments]);
			}
		total_size += sizes[num_segments];
		num_segments++;
	}

	// Segment 3: From CHIP RAM again (different pattern)
	if (g_chip_buf2) {
		sources[num_segments] = g_chip_buf2;
		sizes[num_segments] = SG_SEGMENT_SIZE;
		FillPattern(sources[num_segments], sizes[num_segments], PATTERN_ZEROS);
		if (verbosity > 2) {
			dbgprintf("  Segment %ld: CHIP RAM     0x%08lx -> 0x%08lx (%ld bytes)\n",
		       num_segments, (ULONG)sources[num_segments],
		       (ULONG)(gather_dest + total_size), sizes[num_segments]);
			}
		total_size += sizes[num_segments];
		num_segments++;
	}

	if (num_segments < 2) {
		dbgprintf("ERROR: Need at least 2 memory regions for scatter-gather test\n");
		goto cleanup;
	}

	if (verbosity > 3) 
	{
		dbgprintf("\nExecuting scatter-gather SCRIPTS with %ld segments (%ld bytes total)...\n",
			num_segments, total_size);
		dbgprintf("This will execute %ld Memory Move instructions sequentially\n", num_segments);
		dbgprintf("without any CPU intervention!\n\n");
	}
	// Run the scatter-gather DMA
	status = RunScatterGatherTest(ncr, sources, gather_dest, sizes, num_segments);

	if (status != TEST_SUCCESS) {
		dbgprintf("*** FAILED: Scatter-gather DMA error (status=%ld) ***\n", status);
		all_passed = FALSE;
		goto cleanup;
	}

	// Verify the gathered data
	if (verbosity > 2) dbgprintf("Verifying gathered data...\n");
	offset = 0;
	for (i = 0; i < num_segments; i++) {
		BOOL segment_ok = TRUE;
		for (j = 0; j < sizes[i]; j++) {
			if (gather_dest[offset + j] != sources[i][j]) {
				if (verbosity > 0) {
					dbgprintf("  ERROR: Segment %ld mismatch at offset %ld: "
				       "expected 0x%02lx, got 0x%02lx\n",
				       i, j, (ULONG)sources[i][j],
				       (ULONG)gather_dest[offset + j]);
					}
				segment_ok = FALSE;
				all_passed = FALSE;
				break;
			}
		}
		if (segment_ok) {
			if (verbosity > 1) dbgprintf("  Segment %ld: VERIFIED (%ld bytes)\n", i, sizes[i]);
		}
		offset += sizes[i];
	}

	if (verbosity > 2)
	{
		if (all_passed) {
			dbgprintf("\n*** Scatter-Gather Test: PASSED ***\n");
			dbgprintf("Successfully gathered %ld segments (%ld bytes) from different\n",
				num_segments, total_size);
			dbgprintf("memory regions using a single SCRIPTS program with zero CPU\n");
			dbgprintf("intervention between segments!\n");
		} else {
			dbgprintf("\n*** Scatter-Gather Test: FAILED ***\n");
		}
	}

cleanup:
	if (gather_dest)
		FreeMem(gather_dest, MAX_SG_SEGMENTS * SG_SEGMENT_SIZE);
	if (verify_buf)
		FreeMem(verify_buf, MAX_SG_SEGMENTS * SG_SEGMENT_SIZE);
}


/*
 * Test DMA between different memory types
 */
void TestMemoryTypes(volatile struct ncr710 *ncr)
{
	int src_idx, dst_idx;

	// Define all memory buffers
	struct MemoryBuffer buffers[] = {
		{ &g_chip_buf1,     "CHIP"     },
		{ &g_chip_buf2,     "CHIP"     },
		{ &g_mbfast_buf1,   "MB_FAST"  },
		{ &g_mbfast_buf2,   "MB_FAST"  },
		{ &g_cpufastl_buf1, "CPU_FASTL"},
		{ &g_cpufastl_buf2, "CPU_FASTL"}
//		{ &g_cpufastu_buf1, "CPU_FASTU"},
//		{ &g_cpufastu_buf2, "CPU_FASTU"}
	};
	int num_buffers = sizeof(buffers) / sizeof(buffers[0]);

	atexit(CleanupBuffers);

	dbgprintf("\n=== NCR 53C710 DMA Memory Test Tool ===\n\n");

	// Allocate SCRIPTS buffer in FAST memory
	dbgprintf("Allocating SCRIPTS buffer in FAST memory...\n");
	g_scripts_buf = AllocMem(256, MEMF_FAST | MEMF_CLEAR);
	if (!g_scripts_buf) {
		dbgprintf("ERROR: Could not allocate SCRIPTS buffer\n");
		goto cleanup;
	}
	dbgprintf("  scripts_buf: 0x%08lx %s\n\n", (ULONG)g_scripts_buf,
	       ((ULONG)g_scripts_buf & 3) ? "WARNING: NOT LONGWORD ALIGNED!" : "(aligned)");

	// Allocate chip memory buffers
	dbgprintf("Allocating chip memory buffers...\n");
	g_chip_buf1 = AllocMem(TEST_BUFFER_SIZE, MEMF_CHIP | MEMF_CLEAR);
	g_chip_buf2 = AllocMem(TEST_BUFFER_SIZE, MEMF_CHIP | MEMF_CLEAR);

	if (!g_chip_buf1 || !g_chip_buf2) {
		dbgprintf("ERROR: Could not allocate chip memory buffers\n");
		goto cleanup;
	}

	dbgprintf("  chip_buf1: 0x%08lx %s\n", (ULONG)g_chip_buf1,
	       ((ULONG)g_chip_buf1 & 3) ? "WARNING: NOT LONGWORD ALIGNED!" : "(aligned)");
	dbgprintf("  chip_buf2: 0x%08lx %s\n\n", (ULONG)g_chip_buf2,
	       ((ULONG)g_chip_buf2 & 3) ? "WARNING: NOT LONGWORD ALIGNED!" : "(aligned)");

	// Allocate MB_FAST buffers
	dbgprintf("Allocating MB_FAST buffers...\n");
	g_mbfast_buf1 = AllocInRange(MB_FAST_START, MB_FAST_END, TEST_BUFFER_SIZE+4, "MB_FAST");
	g_mbfast_buf2 = AllocInRange(MB_FAST_START, MB_FAST_END, TEST_BUFFER_SIZE+4, "MB_FAST"); 
	if (g_mbfast_buf1 && g_mbfast_buf2) {
		dbgprintf("  mbfast_buf1: 0x%08lx %s\n", (ULONG)g_mbfast_buf1,
		       ((ULONG)g_mbfast_buf1 & 3) ? "WARNING: NOT LONGWORD ALIGNED!" : "(aligned)");
		dbgprintf("  mbfast_buf2: 0x%08lx %s\n", (ULONG)g_mbfast_buf2,
		       ((ULONG)g_mbfast_buf2 & 3) ? "WARNING: NOT LONGWORD ALIGNED!" : "(aligned)");
	}

	// Allocate CPU_FASTL buffers
	dbgprintf("\nAllocating CPU_FASTL buffers...\n");
	g_cpufastl_buf1 = AllocInRange(CPU_FASTL_START, CPU_FASTL_END, TEST_BUFFER_SIZE, "CPU_FASTL");
	g_cpufastl_buf2 = AllocInRange(CPU_FASTL_START, CPU_FASTL_END, TEST_BUFFER_SIZE, "CPU_FASTL");
	if (g_cpufastl_buf1 && g_cpufastl_buf2) {
		dbgprintf("  cpufastl_buf1: 0x%08lx %s\n", (ULONG)g_cpufastl_buf1,
		       ((ULONG)g_cpufastl_buf1 & 3) ? "WARNING: NOT LONGWORD ALIGNED!" : "(aligned)");
		dbgprintf("  cpufastl_buf2: 0x%08lx %s\n", (ULONG)g_cpufastl_buf2,
		       ((ULONG)g_cpufastl_buf2 & 3) ? "WARNING: NOT LONGWORD ALIGNED!" : "(aligned)");
	}

	// Allocate CPU_FASTU buffers
	dbgprintf("\nAllocating CPU_FASTU buffers...\n");
	g_cpufastu_buf1 = AllocInRange(CPU_FASTU_START, CPU_FASTU_END, TEST_BUFFER_SIZE, "CPU_FASTU");
	g_cpufastu_buf2 = AllocInRange(CPU_FASTU_START, CPU_FASTU_END, TEST_BUFFER_SIZE, "CPU_FASTU");
	if (g_cpufastu_buf1 && g_cpufastu_buf2) {
		dbgprintf("  cpufastu_buf1: 0x%08lx %s\n", (ULONG)g_cpufastu_buf1,
		       ((ULONG)g_cpufastu_buf1 & 3) ? "WARNING: NOT LONGWORD ALIGNED!" : "(aligned)");
		dbgprintf("  cpufastu_buf2: 0x%08lx %s\n", (ULONG)g_cpufastu_buf2,
		       ((ULONG)g_cpufastu_buf2 & 3) ? "WARNING: NOT LONGWORD ALIGNED!" : "(aligned)");
	}

	dbgprintf("\n=== Starting DMA Tests ===\n");

	// Test all permutations: every buffer to every other buffer
	for (src_idx = 0; src_idx < num_buffers; src_idx++) {
		for (dst_idx = 0; dst_idx < num_buffers; dst_idx++) {
			// Skip if source and destination are the same buffer
			if (src_idx == dst_idx)
				continue;

			TestDMATransfer(ncr,
			                *buffers[src_idx].buf, buffers[src_idx].name,
			                *buffers[dst_idx].buf, buffers[dst_idx].name);
		}
	}

	dbgprintf("\n=== Basic Tests Complete ===\n\n");

	dbgprintf("\n=== Scatter Gather Testing ===\n\n");

	// Run scatter-gather tests
	TestScatterGather(ncr, 1);

	for (int i = 0; i < 1000; i++)
	{
		// Run scatter-gather tests
		TestScatterGather(ncr, 0);		
	}

	dbgprintf("\n=== Scatter Gather Tests Complete ===\n\n");


cleanup:
	CleanupBuffers();
}

/*
 * Main test entry point
 */
void TestMain(void)
{
	volatile struct ncr710 *ncr;

	ncr = (volatile struct ncr710 *)NCR_ADDRESS;

	dbgprintf("NCR chip at: 0x%08lx\n", (ULONG)ncr);

	// Initialize the NCR chip
	if (InitNCR(ncr) < 0) {
		dbgprintf("FATAL: NCR initialization failed\n");
		return;
	}

	// Run the tests
	TestMemoryTypes(ncr);
}
