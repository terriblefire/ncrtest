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

/* External functions */
extern LONG InitNCR(volatile struct ncr710 *ncr);
extern LONG CheckNCRStatus(volatile struct ncr710 *ncr, const char *context);

/* Global buffers for cleanup on CTRL-C */
static UBYTE *g_chip_buf1 = NULL;
static UBYTE *g_chip_buf2 = NULL;
static UBYTE *g_fast_buf1 = NULL;
static UBYTE *g_fast_buf2 = NULL;
static BOOL g_cleanup_done = FALSE;

/* Simple pseudo-random number generator for test patterns */
static ULONG random_seed = 0x12345678;

/*
 * Cleanup function for CTRL-C or normal exit
 */
static void CleanupBuffers(void)
{
	if (g_cleanup_done)
		return;

	printf("\nCleaning up buffers...\n");

	if (g_chip_buf1) {
		FreeMem(g_chip_buf1, TEST_BUFFER_SIZE);
		g_chip_buf1 = NULL;
	}
	if (g_chip_buf2) {
		FreeMem(g_chip_buf2, TEST_BUFFER_SIZE);
		g_chip_buf2 = NULL;
	}
	if (g_fast_buf1) {
		FreeMem(g_fast_buf1, TEST_BUFFER_SIZE);
		g_fast_buf1 = NULL;
	}
	if (g_fast_buf2) {
		FreeMem(g_fast_buf2, TEST_BUFFER_SIZE);
		g_fast_buf2 = NULL;
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
}

/*
 * Verify that destination buffer matches source buffer
 */
LONG VerifyBuffer(UBYTE *src, UBYTE *dst, ULONG size, struct TestResult *result)
{
	ULONG i;

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
 */
static ULONG* BuildDMAScript(UBYTE *src, UBYTE *dst, ULONG size)
{
	static struct {
		struct memmove_inst move;
		struct jump_inst    done;
	} script;

	// Memory-to-memory move instruction
	script.move.op = 0xC0;  // Memory move opcode
	script.move.len[0] = (size >> 16) & 0xFF;
	script.move.len[1] = (size >> 8) & 0xFF;
	script.move.len[2] = size & 0xFF;
	script.move.source = (ULONG)src;
	script.move.dest = (ULONG)dst;

	// INT instruction to signal completion (opcode 0x98, interrupt on the fly)
	script.done.op = 0x98;
	script.done.control = 0x08;  // Interrupt always
	script.done.mask = 0x00;
	script.done.data = 0x00;
	script.done.addr = 0xDEADBEEF;  // Value that will be in DSPS

	return (ULONG*)&script;
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

	printf("ERROR: DMA timeout\n");
	printf("  ISTAT: 0x%02lx\n", (ULONG)ncr->istat);
	printf("  DSTAT: 0x%02lx\n", (ULONG)ncr->dstat);
	printf("  DSP:   0x%08lx\n", ncr->dsp);

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
		printf("  FAILED Test #%ld: Pattern=%s Size=%ld Status=%s",
		        result->test_number,
		        pattern_names[result->pattern_type],
		        result->size,
		        status_names[result->status]);

		if (result->status == TEST_VERIFY_ERROR) {
			printf("\n    ERROR at offset 0x%lx: Expected=0x%02lx Actual=0x%02lx",
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

	/*printf("\n=== Starting Comprehensive DMA Tests ===\n");
	printf("Running tests");*/

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
		printf("\n=== Some tests failed ===\n");
		printf("Source buffer: 0x%08lx\n", (ULONG)src_base);
		printf("Dest buffer:   0x%08lx\n", (ULONG)dst_base);
		printf("Buffer size:   %ld bytes\n", buffer_size);
		printf("\n");
	
		printf("\n=== Test Summary ===\n");
		printf("Total tests: %ld\n", test_num);
		printf("Passed:      %ld\n", passed);
		printf("Failed:      %ld\n", failed);
	}

	return (failed == 0) ? 0 : -1;
}

/*
 * Test DMA between different memory types
 */
void TestMemoryTypes(volatile struct ncr710 *ncr)
{
	atexit(CleanupBuffers);

	printf("\n=== NCR 53C710 DMA Memory Test Tool ===\n\n");

	// Allocate chip memory buffers using globals for cleanup
	// Note: AllocMem returns longword-aligned memory by default
	g_chip_buf1 = AllocMem(TEST_BUFFER_SIZE, MEMF_CHIP | MEMF_CLEAR);
	g_chip_buf2 = AllocMem(TEST_BUFFER_SIZE, MEMF_CHIP | MEMF_CLEAR);

	// Allocate fast memory buffers
	g_fast_buf1 = AllocMem(TEST_BUFFER_SIZE, MEMF_FAST | MEMF_CLEAR);
	g_fast_buf2 = AllocMem(TEST_BUFFER_SIZE, MEMF_FAST | MEMF_CLEAR);

	// If we couldn't get FAST, try PUBLIC (any memory)
	if (!g_fast_buf1)
		g_fast_buf1 = AllocMem(TEST_BUFFER_SIZE, MEMF_PUBLIC | MEMF_CLEAR);
	if (!g_fast_buf2)
		g_fast_buf2 = AllocMem(TEST_BUFFER_SIZE, MEMF_PUBLIC | MEMF_CLEAR);

	if (!g_chip_buf1 || !g_chip_buf2) {
		printf("ERROR: Could not allocate chip memory buffers\n");
		goto cleanup;
	}

	if (!g_fast_buf1 || !g_fast_buf2) {
		printf("ERROR: Could not allocate fast memory buffers\n");
		goto cleanup;
	}

	// Verify buffer alignment (should be longword-aligned)
	printf("Buffer addresses:\n");
	printf("  chip_buf1: 0x%08lx %s\n", (ULONG)g_chip_buf1,
	       ((ULONG)g_chip_buf1 & 3) ? "WARNING: NOT LONGWORD ALIGNED!" : "(aligned)");
	printf("  chip_buf2: 0x%08lx %s\n", (ULONG)g_chip_buf2,
	       ((ULONG)g_chip_buf2 & 3) ? "WARNING: NOT LONGWORD ALIGNED!" : "(aligned)");
	printf("  fast_buf1: 0x%08lx %s\n", (ULONG)g_fast_buf1,
	       ((ULONG)g_fast_buf1 & 3) ? "WARNING: NOT LONGWORD ALIGNED!" : "(aligned)");
	printf("  fast_buf2: 0x%08lx %s\n\n", (ULONG)g_fast_buf2,
	       ((ULONG)g_fast_buf2 & 3) ? "WARNING: NOT LONGWORD ALIGNED!" : "(aligned)");

	for (int iteration=0; iteration<5000; iteration++)
	{
		printf("Iteration %d/5000\n", iteration+1);
		// Run tests for each memory type combination

		// Test 1: CHIP -> CHIP
		printf("*** Test 1: CHIP MEMORY -> CHIP MEMORY ***\n");
		RunComprehensiveTest(ncr, g_chip_buf1, g_chip_buf2, TEST_BUFFER_SIZE);

		// Test 2: FAST -> FAST
		printf("*** Test 2: FAST MEMORY -> FAST MEMORY ***\n");
		RunComprehensiveTest(ncr, g_fast_buf1, g_fast_buf2, TEST_BUFFER_SIZE);

		// Test 3: CHIP -> FAST
		printf("*** Test 3: CHIP MEMORY -> FAST MEMORY ***\n");
		RunComprehensiveTest(ncr, g_chip_buf1, g_fast_buf2, TEST_BUFFER_SIZE);

		// Test 4: FAST -> CHIP
		printf("*** Test 4: FAST MEMORY -> CHIP MEMORY ***\n");
		RunComprehensiveTest(ncr, g_fast_buf1, g_chip_buf2, TEST_BUFFER_SIZE);

	}

	printf("\n=== All DMA Tests Complete ===\n\n");

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

	printf("NCR chip at: 0x%08lx\n", (ULONG)ncr);

	// Initialize the NCR chip
	if (InitNCR(ncr) < 0) {
		printf("FATAL: NCR initialization failed\n");
		return;
	}

	// Run the tests
	TestMemoryTypes(ncr);
}
