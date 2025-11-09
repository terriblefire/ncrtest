/*
 * ncr_scsi.c - SCSI INQUIRY Command Implementation
 * Based on Kickstart ROM NCR 53C710 driver
 */

#include "ncr_scsi.h"
#include <stdio.h>
#include <string.h>
#include <exec/execbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>

/* External functions from ncr_init.c */
extern LONG InitNCR(volatile struct ncr710 *ncr);
extern void kprintf(char *,...);

/* Global interrupt state and structures */
static struct NCRIntState g_int_state;
static struct Interrupt g_int_server;
static volatile struct ncr710 *g_ncr_chip;
extern void dbgprintf(const char *format, ...);

/* NCR chip address (A4000T) */
#define NCR_ADDRESS 0x00DD0040

/* Write offset for longword writes */
#define NCR_WRITE_OFFSET 0x80

/* Pseudo-random number generator for data patterns */
static ULONG g_random_seed = 0x12345678;

/*
 * Simple LCG pseudo-random number generator
 * Same algorithm as ncr_dmatest.c for consistency
 */
static ULONG
GetRandom(void)
{
	g_random_seed = g_random_seed * 1103515245 + 12345;
	return g_random_seed;
}

/*
 * Reset PRNG to initial seed
 */
static void
ResetRandom(void)
{
	g_random_seed = 0x12345678;
}

/*
 * Fill buffer with pseudo-random data
 */
static void
FillRandomData(UBYTE *buffer, ULONG size)
{
	ULONG i;
	ULONG random_val;

	for (i = 0; i < size; i++) {
		if ((i & 3) == 0) {
			random_val = GetRandom();
		}
		buffer[i] = (random_val >> ((i & 3) * 8)) & 0xFF;
	}
}

/*
 * Verify buffer against pseudo-random pattern
 * Returns 0 on success, offset+1 on mismatch
 */
static LONG
VerifyRandomData(UBYTE *buffer, ULONG size, ULONG *error_offset)
{
	ULONG i;
	ULONG random_val;
	UBYTE expected;

	for (i = 0; i < size; i++) {
		if ((i & 3) == 0) {
			random_val = GetRandom();
		}
		expected = (random_val >> ((i & 3) * 8)) & 0xFF;

		if (buffer[i] != expected) {
			*error_offset = i;
			dbgprintf("ERROR: Mismatch at offset 0x%08lx\n", i);
			dbgprintf("  Expected: 0x%02lx\n", (ULONG)expected);
			dbgprintf("  Got:      0x%02lx\n", (ULONG)buffer[i]);
			return i + 1;  // Return offset+1 (0 = success)
		}
	}

	return 0;  // Success
}

/* SCRIPTS program for INQUIRY command */
/* Based on ROM driver's SCRIPTS (script.c) with correct instruction encoding */
static ULONG inquiry_script[] = {
	// Entry point - start selection
	// SELECT with ATN, table indirect addressing (opcode 0x47 from ROM)
	// Address is offset to select_data (offset 20 = 0x14)
	0x47000014, 0x00000000,		// SELECT ATN FROM select_data, REL(failed)

	// Send IDENTIFY message (opcode 0x1E = MSG_OUT phase)
	// Address is offset to send_msg (offset 40 = 0x28)
	0x1E000000, 0x00000028,		// MOVE FROM send_msg, WHEN MSG_OUT

	// Send INQUIRY command (opcode 0x1A = COMMAND phase)
	// Address is offset to command_data (offset 48 = 0x30)
	0x1A000000, 0x00000030,		// MOVE FROM command_data, WHEN COMMAND

	// Receive INQUIRY data (opcode 0x19 = DATA_IN phase)
	// Address is offset to move_data (offset 0 = 0x00)
	0x19000000, 0x00000000,		// MOVE FROM move_data, WHEN DATA_IN

	// Get status byte (opcode 0x1B = STATUS phase)
	// Address is offset to status_data (offset 24 = 0x18)
	0x1B000000, 0x00000018,		// MOVE FROM status_data, WHEN STATUS

	// Get message byte (opcode 0x1F = MSG_IN phase)
	// Address is offset to recv_msg (offset 32 = 0x20)
	0x1F000000, 0x00000020,		// MOVE FROM recv_msg, WHEN MSG_IN

	// Clear ACK and wait for disconnect
	0x60000040, 0x00000000,		// CLEAR ACK
	0x48000000, 0x00000000,		// WAIT DISCONNECT

	// Signal completion
	0x98080000, 0xDEADBEEF,		// INT 0xDEADBEEF

	// Error handler (selection failed) - relative jump target
	0x98080000, 0xBADBAD00,		// INT 0xBADBAD00 (failed label)
};

/*
 * NCR 53C710 Interrupt Handler
 * Called when the NCR chip generates an interrupt
 * __saveds preserves registers, a1 contains the is_Data pointer
 */
static ULONG __attribute__((saveds))
NCRInterruptHandler(void)
{
	volatile struct ncr710 *ncr = g_ncr_chip;
	UBYTE istat;

	// Check if this is our interrupt
	istat = ncr->istat;

	// Debug: always increment counter to see if handler is called
	static volatile ULONG handler_call_count = 0;
	handler_call_count++;

	if (!(istat & (ISTATF_SIP | ISTATF_DIP))) {
		return 0;  // Not our interrupt
	}

	// Save interrupt status
	g_int_state.istat = istat;
	g_int_state.int_received = 1;

	// Read and save DMA status if DMA interrupt
	if (istat & ISTATF_DIP) {
		g_int_state.dstat = ncr->dstat;
		g_int_state.dsps = ncr->dsps;
	}

	// Read and save SCSI status if SCSI interrupt
	if (istat & ISTATF_SIP) {
		g_int_state.sstat0 = ncr->sstat0;
		// Also read sstat1 and sstat2 to clear interrupts
		(void)ncr->sstat1;
		(void)ncr->sstat2;
	}

	// Signal our task
	Signal(g_int_state.task, g_int_state.signal_mask);

	return 1;  // Interrupt handled
}

/*
 * Setup NCR interrupts
 */
LONG
SetupNCRInterrupts(volatile struct ncr710 *ncr)
{
	LONG signal_bit;

	dbgprintf("Setting up NCR interrupts...\n");

	// Save NCR chip pointer for interrupt handler
	g_ncr_chip = ncr;

	// Allocate a signal bit
	signal_bit = AllocSignal(-1);
	if (signal_bit == -1) {
		dbgprintf("ERROR: Could not allocate signal\n");
		return -1;
	}

	// Initialize interrupt state
	g_int_state.task = FindTask(NULL);
	g_int_state.signal_mask = 1L << signal_bit;
	g_int_state.int_received = 0;

	// Setup interrupt server structure
	g_int_server.is_Node.ln_Type = NT_INTERRUPT;
	g_int_server.is_Node.ln_Pri = 127;  // High priority
	g_int_server.is_Node.ln_Name = "NCR 53C710 SCSI";
	g_int_server.is_Data = (APTR)&g_int_state;
	g_int_server.is_Code = (VOID (*)())NCRInterruptHandler;

	// Atomic interrupt setup: disable all interrupts during setup
	Disable();

	// Clear all pending interrupts before adding handler
	(void)ncr->istat;
	(void)ncr->dstat;
	(void)ncr->sstat0;
	(void)ncr->sstat1;
	(void)ncr->sstat2;

	// Add interrupt server to the chain
	AddIntServer(NCR_INTNUM, &g_int_server);

	// Now enable NCR interrupts (after handler is installed)
	// Enable DMA interrupts: SCRIPTS interrupt, illegal instruction, abort
	ncr->dien = DIENF_SIR | DIENF_IID | DIENF_ABRT;

	// Enable SCSI interrupts (but not SEL/FCMP which are noisy)
	ncr->sien = (UBYTE)~(SIENF_FCMP | SIENF_SEL);

	// Re-enable all interrupts
	Enable();

	dbgprintf("  Signal bit: %ld\n", signal_bit);
	dbgprintf("  Interrupt server added\n");
	dbgprintf("  NCR interrupts enabled (DIEN=0x%02lx, SIEN=0x%02lx)\n",
	          (ULONG)ncr->dien, (ULONG)ncr->sien);
	dbgprintf("Interrupt setup complete\n\n");

	return 0;
}

/*
 * Cleanup NCR interrupts
 */
void
CleanupNCRInterrupts(volatile struct ncr710 *ncr)
{
	LONG signal_bit;

	dbgprintf("Cleaning up NCR interrupts...\n");

	// Disable NCR interrupts
	ncr->dien = 0;
	ncr->sien = 0;

	// Remove interrupt server
	RemIntServer(NCR_INTNUM, &g_int_server);

	// Free signal
	for (signal_bit = 0; signal_bit < 32; signal_bit++) {
		if (g_int_state.signal_mask == (1L << signal_bit)) {
			FreeSignal(signal_bit);
			break;
		}
	}

	dbgprintf("Interrupt cleanup complete\n");
}

/*
 * Initialize NCR chip for SCSI bus operations
 * Based on init_chip() from ROM driver (ncr.c:920)
 * This enables the SCSI bus, unlike InitNCR() which only sets up DMA
 */
LONG
InitNCRForSCSI(volatile struct ncr710 *ncr)
{
	dbgprintf("Enabling SCSI bus operations...\n");

	// Enable parity generation (from ROM driver)
	dbgprintf("  Enabling parity generation...\n");
	ncr->scntl0 |= SCNTL0F_EPG;

	// Assert SCSI bus reset (required by SCSI spec)
	dbgprintf("  Asserting SCSI bus reset...\n");
	ncr->scntl1 = SCNTL1F_RST;

	// Wait 25us before de-asserting RST (SCSI spec requirement)
	poll_cia(25);

	// De-assert SCSI bus reset
	dbgprintf("  De-asserting SCSI bus reset...\n");
	ncr->scntl1 &= ~SCNTL1F_RST;

	// Wait 250ms before using the bus (SCSI spec requirement)
	dbgprintf("  Waiting for bus to stabilize...\n");
	poll_cia(250000);

	// Set our SCSI ID (ID 7 is typical for initiator)
	dbgprintf("  Setting SCSI ID to %ld...\n", (ULONG)NCR_SCSI_ID);
	ncr->scid = 1 << NCR_SCSI_ID;

	// Enable Selection/Reselection
	dbgprintf("  Enabling Selection/Reselection...\n");
	ncr->scntl1 |= SCNTL1F_ESR;

	// Disable halt on parity error (asynch mode)
	dbgprintf("  Configuring sync transfer register...\n");
	ncr->sxfer = SXFERF_DHP;

	dbgprintf("SCSI bus enabled successfully\n\n");
	return 0;
}

/*
 * Build DSA entry for INQUIRY command
 * Based on ROM driver's DSA setup
 */
static void
BuildInquiryDSA(struct DSA_entry *dsa, UBYTE target_id, UBYTE *data_buf)
{
	// Clear entire DSA
	memset(dsa, 0, sizeof(struct DSA_entry));

	// Setup data move (36 bytes of INQUIRY data)
	dsa->move_data.len  = 36;		// INQUIRY returns 36 bytes
	dsa->move_data.addr = (ULONG)data_buf;

	// Setup selection data (ID and sync value)
	dsa->select_data.res1 = 0;
	dsa->select_data.id   = (1 << target_id);  // Bitmask for target
	dsa->select_data.sync = 0;		   // Async transfer
	dsa->select_data.res2 = 0;

	// Setup status byte location
	dsa->status_data.len  = 1;
	dsa->status_data.addr = (ULONG)&(dsa->status_buf[0]);

	// Setup message in location
	dsa->recv_msg.len  = 1;
	dsa->recv_msg.addr = (ULONG)&(dsa->recv_buf[0]);

	// Setup message out (IDENTIFY + LUN)
	dsa->send_msg.len  = 1;
	dsa->send_msg.addr = (ULONG)&(dsa->send_buf[0]);
	dsa->send_buf[0] = MSG_IDENTIFY;  // IDENTIFY message, LUN 0

	// Setup INQUIRY command (6 bytes)
	dsa->command_data.len  = 6;
	dsa->command_data.addr = (ULONG)&(dsa->send_buf[1]);
	dsa->send_buf[1] = S_INQUIRY;	// INQUIRY opcode
	dsa->send_buf[2] = 0x00;	// LUN = 0
	dsa->send_buf[3] = 0x00;	// Page code = 0
	dsa->send_buf[4] = 0x00;	// Reserved
	dsa->send_buf[5] = 36;		// Allocation length = 36 bytes
	dsa->send_buf[6] = 0x00;	// Control
}

/*
 * Execute INQUIRY command
 * Returns: 0 on success, negative on error
 */
LONG
DoInquiry(volatile struct ncr710 *ncr, UBYTE target_id, struct InquiryData *data)
{
	struct DSA_entry *dsa;
	UBYTE istat, dstat;
	LONG result = -1;

	dbgprintf("\n=== SCSI INQUIRY Command ===\n");
	dbgprintf("Target ID: %ld\n", (ULONG)target_id);

	// Allocate DSA in FAST memory (like ROM driver)
	dsa = AllocMem(sizeof(struct DSA_entry), MEMF_FAST | MEMF_CLEAR);
	if (!dsa) {
		dbgprintf("ERROR: Could not allocate DSA\n");
		return -1;
	}

	dbgprintf("DSA allocated at: 0x%08lx\n", (ULONG)dsa);

	// Build DSA for INQUIRY
	BuildInquiryDSA(dsa, target_id, (UBYTE *)data);

	// Flush caches (like ROM driver does with CachePreDMA)
	CacheClearU();

	// Load DSA register with our DSA address (like ROM driver)
	WRITE_LONG(ncr, dsa, (ULONG)dsa);

	// Clear any pending interrupts
	(void)ncr->istat;
	(void)ncr->dstat;
	(void)ncr->sstat0;

	dbgprintf("Starting SCRIPTS execution...\n");

	// Clear interrupt received flag
	g_int_state.int_received = 0;

	// Start SCRIPTS execution (load DSP)
	WRITE_LONG(ncr, dsp, (ULONG)inquiry_script);

	// Wait for interrupt (with Ctrl-C break)
	dbgprintf("Waiting for interrupt (signal mask 0x%08lx)...\n", g_int_state.signal_mask);
	ULONG sigs = Wait(g_int_state.signal_mask | SIGBREAKF_CTRL_C);
	dbgprintf("Got signal: 0x%08lx, int_received=%ld\n", sigs, g_int_state.int_received);

	if (sigs & SIGBREAKF_CTRL_C) {
		dbgprintf("ERROR: Interrupted by user (Ctrl-C)\n");
		result = -8;
	} else if (!(sigs & g_int_state.signal_mask)) {
		dbgprintf("ERROR: Spurious signal (expected 0x%08lx, got 0x%08lx)\n",
		          g_int_state.signal_mask, sigs);
		result = -9;
	} else if (!g_int_state.int_received) {
		dbgprintf("ERROR: Got our signal but int_received not set (handler didn't run?)\n");
		dbgprintf("  ISTAT: 0x%02lx\n", (ULONG)ncr->istat);
		dbgprintf("  DSTAT: 0x%02lx\n", (ULONG)ncr->dstat);
		dbgprintf("  SSTAT0: 0x%02lx\n", (ULONG)ncr->sstat0);
		result = -10;
	} else {
		// Check interrupt status from handler
		istat = g_int_state.istat;

		// Check for DMA interrupt
		if (istat & ISTATF_DIP) {
			dstat = g_int_state.dstat;

			// Check for SCRIPTS interrupt
			if (dstat & DSTATF_SIR) {
				ULONG dsps = g_int_state.dsps;

				if (dsps == 0xDEADBEEF) {
					// Success!
					dbgprintf("SCRIPTS completed successfully\n");
					dbgprintf("Status byte: 0x%02lx\n",
					          (ULONG)dsa->status_buf[0]);
					dbgprintf("Message byte: 0x%02lx\n",
					          (ULONG)dsa->recv_buf[0]);

					if (dsa->status_buf[0] == SCSI_GOOD) {
						result = 0;  // Success
					} else {
						dbgprintf("ERROR: Bad status (0x%02lx)\n",
						          (ULONG)dsa->status_buf[0]);
						result = -2;
					}
				} else if (dsps == 0xBADBAD00) {
					dbgprintf("ERROR: Selection failed\n");
					result = -3;
				} else {
					dbgprintf("ERROR: Unexpected interrupt (0x%08lx)\n", dsps);
					result = -4;
				}
			}

			// Check for errors
			if (dstat & (DSTATF_IID | DSTATF_ABRT | DSTATF_SSI)) {
				dbgprintf("ERROR: DMA error (DSTAT=0x%02lx)\n", (ULONG)dstat);
				result = -5;
			}
		}

		// Check for SCSI interrupt
		if (istat & ISTATF_SIP) {
			dbgprintf("ERROR: SCSI interrupt (SSTAT0=0x%02lx)\n",
			          (ULONG)g_int_state.sstat0);
			result = -6;
		}
	}

	// Flush caches after DMA (like ROM driver's CachePostDMA)
	CacheClearU();

	// Free DSA
	FreeMem(dsa, sizeof(struct DSA_entry));

	return result;
}

/*
 * Print INQUIRY data in human-readable format
 */
void
PrintInquiryData(struct InquiryData *data)
{
	char vendor[9], product[17], revision[5];
	const char *device_types[] = {
		"Direct-access (disk)",
		"Sequential-access (tape)",
		"Printer",
		"Processor",
		"Write-once",
		"CD-ROM",
		"Scanner",
		"Optical memory",
		"Medium changer",
		"Communications"
	};

	// Copy and null-terminate strings
	memcpy(vendor, data->vendor, 8);
	vendor[8] = '\0';

	memcpy(product, data->product, 16);
	product[16] = '\0';

	memcpy(revision, data->revision, 4);
	revision[4] = '\0';

	dbgprintf("\n=== INQUIRY Results ===\n");
	dbgprintf("Device Type:  %ld (%s)\n",
	          (ULONG)data->device_type,
	          data->device_type < 10 ? device_types[data->device_type] : "Unknown");
	dbgprintf("Removable:    %s\n", (data->removable & 0x80) ? "Yes" : "No");
	dbgprintf("SCSI Version: %ld\n", (ULONG)data->version);
	dbgprintf("Vendor:       '%s'\n", vendor);
	dbgprintf("Product:      '%s'\n", product);
	dbgprintf("Revision:     '%s'\n", revision);
	dbgprintf("\n");
}

/*
 * Build DSA entry for READ(10) command
 */
static void
BuildRead10DSA(struct DSA_entry *dsa, UBYTE target_id, ULONG lba, UWORD blocks, UBYTE *data_buf)
{
	// Clear entire DSA
	memset(dsa, 0, sizeof(struct DSA_entry));

	// Setup data move (blocks * 512 bytes)
	dsa->move_data.len  = blocks * SCSI_BLOCK_SIZE;
	dsa->move_data.addr = (ULONG)data_buf;

	// Setup selection data (ID and sync value)
	dsa->select_data.res1 = 0;
	dsa->select_data.id   = (1 << target_id);
	dsa->select_data.sync = 0;		// Async transfer
	dsa->select_data.res2 = 0;

	// Setup status byte location
	dsa->status_data.len  = 1;
	dsa->status_data.addr = (ULONG)&(dsa->status_buf[0]);

	// Setup message in location
	dsa->recv_msg.len  = 1;
	dsa->recv_msg.addr = (ULONG)&(dsa->recv_buf[0]);

	// Setup message out (IDENTIFY + LUN)
	dsa->send_msg.len  = 1;
	dsa->send_msg.addr = (ULONG)&(dsa->send_buf[0]);
	dsa->send_buf[0] = MSG_IDENTIFY;

	// Setup READ(10) command (10 bytes)
	dsa->command_data.len  = 10;
	dsa->command_data.addr = (ULONG)&(dsa->send_buf[1]);
	dsa->send_buf[1] = S_READ10;		// READ(10) opcode
	dsa->send_buf[2] = 0x00;		// LUN = 0, flags
	dsa->send_buf[3] = (lba >> 24) & 0xFF;	// LBA byte 0 (MSB)
	dsa->send_buf[4] = (lba >> 16) & 0xFF;	// LBA byte 1
	dsa->send_buf[5] = (lba >> 8) & 0xFF;	// LBA byte 2
	dsa->send_buf[6] = lba & 0xFF;		// LBA byte 3 (LSB)
	dsa->send_buf[7] = 0x00;		// Reserved
	dsa->send_buf[8] = (blocks >> 8) & 0xFF; // Transfer length MSB
	dsa->send_buf[9] = blocks & 0xFF;	// Transfer length LSB
	dsa->send_buf[10] = 0x00;		// Control
}

/*
 * Execute READ(10) command for a chunk
 * Returns: 0 on success, negative on error
 */
static LONG
DoRead10Chunk(volatile struct ncr710 *ncr, UBYTE target_id, ULONG lba, UWORD blocks, UBYTE *data_buf)
{
	struct DSA_entry *dsa;
	UBYTE istat, dstat;
	LONG result = -1;

	// Allocate DSA in FAST memory
	dsa = AllocMem(sizeof(struct DSA_entry), MEMF_FAST | MEMF_CLEAR);
	if (!dsa) {
		dbgprintf("ERROR: Could not allocate DSA\n");
		return -1;
	}

	// Build DSA for READ(10)
	BuildRead10DSA(dsa, target_id, lba, blocks, data_buf);

	// Flush caches
	CacheClearU();

	// Load DSA register
	WRITE_LONG(ncr, dsa, (ULONG)dsa);

	// Clear any pending interrupts
	(void)ncr->istat;
	(void)ncr->dstat;
	(void)ncr->sstat0;

	// Clear interrupt received flag
	g_int_state.int_received = 0;

	// Start SCRIPTS execution (reuse same SCRIPTS as INQUIRY)
	WRITE_LONG(ncr, dsp, (ULONG)inquiry_script);

	// Wait for interrupt
	ULONG sigs = Wait(g_int_state.signal_mask | SIGBREAKF_CTRL_C);

	if (sigs & SIGBREAKF_CTRL_C) {
		dbgprintf("ERROR: Interrupted by user (Ctrl-C)\n");
		result = -8;
	} else if (!g_int_state.int_received) {
		dbgprintf("ERROR: Spurious signal\n");
		result = -9;
	} else {
		// Check interrupt status from handler
		istat = g_int_state.istat;

		// Check for DMA interrupt
		if (istat & ISTATF_DIP) {
			dstat = g_int_state.dstat;

			// Check for SCRIPTS interrupt
			if (dstat & DSTATF_SIR) {
				ULONG dsps = g_int_state.dsps;

				if (dsps == 0xDEADBEEF) {
					// Success!
					if (dsa->status_buf[0] == SCSI_GOOD) {
						result = 0;
					} else {
						dbgprintf("ERROR: Bad status (0x%02lx)\n",
						          (ULONG)dsa->status_buf[0]);
						result = -2;
					}
				} else if (dsps == 0xBADBAD00) {
					dbgprintf("ERROR: Selection failed\n");
					result = -3;
				} else {
					dbgprintf("ERROR: Unexpected interrupt (0x%08lx)\n", dsps);
					result = -4;
				}
			}

			// Check for errors
			if (dstat & (DSTATF_IID | DSTATF_ABRT | DSTATF_SSI)) {
				dbgprintf("ERROR: DMA error (DSTAT=0x%02lx)\n", (ULONG)dstat);
				result = -5;
			}
		}

		// Check for SCSI interrupt
		if (istat & ISTATF_SIP) {
			dbgprintf("ERROR: SCSI interrupt (SSTAT0=0x%02lx)\n",
			          (ULONG)g_int_state.sstat0);
			result = -6;
		}
	}

	// Flush caches after DMA
	CacheClearU();

	// Free DSA
	FreeMem(dsa, sizeof(struct DSA_entry));

	return result;
}

/*
 * Read first 32MB from SCSI disk into FAST memory
 * Returns: 0 on success, negative on error
 */
LONG
DoRead32MB(volatile struct ncr710 *ncr, UBYTE target_id)
{
	UBYTE *buffer;
	ULONG lba;
	ULONG total_blocks = READ_32MB_BLOCKS;
	ULONG blocks_read = 0;
	LONG result;

	dbgprintf("\n=== Reading 32MB from SCSI ID %ld ===\n", (ULONG)target_id);
	dbgprintf("Total blocks: %ld (%ld bytes)\n", total_blocks, READ_32MB_SIZE);
	dbgprintf("Chunk size: %ld blocks (%ld bytes)\n\n",
	          (ULONG)READ_CHUNK_BLOCKS, (ULONG)READ_CHUNK_SIZE);

	// Allocate 32MB buffer in FAST memory
	dbgprintf("Allocating 32MB FAST memory buffer...\n");
	buffer = AllocMem(READ_32MB_SIZE, MEMF_FAST);
	if (!buffer) {
		dbgprintf("ERROR: Could not allocate 32MB FAST memory\n");
		dbgprintf("Trying CHIP memory instead...\n");
		buffer = AllocMem(READ_32MB_SIZE, MEMF_CHIP);
		if (!buffer) {
			dbgprintf("ERROR: Could not allocate 32MB memory at all\n");
			return -1;
		}
	}

	dbgprintf("Buffer allocated at: 0x%08lx\n\n", (ULONG)buffer);

	// Read in chunks
	lba = 0;
	while (blocks_read < total_blocks) {
		ULONG blocks_to_read = READ_CHUNK_BLOCKS;
		UBYTE *chunk_buf = buffer + (blocks_read * SCSI_BLOCK_SIZE);

		// Adjust last chunk if needed
		if (blocks_read + blocks_to_read > total_blocks) {
			blocks_to_read = total_blocks - blocks_read;
		}

		dbgprintf("Reading LBA %ld, %ld blocks (%ld KB)... ",
		          lba, blocks_to_read, (blocks_to_read * SCSI_BLOCK_SIZE) / 1024);

		result = DoRead10Chunk(ncr, target_id, lba, blocks_to_read, chunk_buf);

		if (result != 0) {
			dbgprintf("FAILED (error %ld)\n", result);
			dbgprintf("\nRead failed at block %ld\n", blocks_read);
			FreeMem(buffer, READ_32MB_SIZE);
			return result;
		}

		dbgprintf("OK\n");

		lba += blocks_to_read;
		blocks_read += blocks_to_read;

		// Show progress every 1MB
		if ((blocks_read % (2048)) == 0) {
			dbgprintf("  Progress: %ld MB / 32 MB\n", blocks_read / 2048);
		}
	}

	dbgprintf("\n=== Read Complete ===\n");
	dbgprintf("Total read: %ld blocks (%ld MB)\n", blocks_read, blocks_read / 2048);
	dbgprintf("Buffer at: 0x%08lx - 0x%08lx\n",
	          (ULONG)buffer, (ULONG)(buffer + READ_32MB_SIZE - 1));

	// Verify data against pseudo-random pattern
	dbgprintf("\n=== Verifying Data ===\n");
	dbgprintf("Checking 32MB against PRNG pattern...\n");

	ResetRandom();  // Start with same seed
	ULONG error_offset = 0;
	result = VerifyRandomData(buffer, READ_32MB_SIZE, &error_offset);

	if (result != 0) {
		dbgprintf("\n*** VERIFICATION FAILED ***\n");
		dbgprintf("Mismatch at offset: 0x%08lx (block %ld, byte %ld)\n",
		          error_offset,
		          error_offset / SCSI_BLOCK_SIZE,
		          error_offset % SCSI_BLOCK_SIZE);

		// Show hex dump around error
		ULONG dump_start = (error_offset & ~0xF);  // Align to 16 bytes
		if (dump_start > 64) dump_start -= 64;
		else dump_start = 0;

		dbgprintf("\nData around error (offset 0x%08lx):\n", dump_start);
		for (ULONG i = dump_start; i < dump_start + 128 && i < READ_32MB_SIZE; i += 16) {
			dbgprintf("%08lx: ", i);
			for (ULONG j = 0; j < 16 && i + j < READ_32MB_SIZE; j++) {
				dbgprintf("%02lx ", (ULONG)buffer[i + j]);
			}
			dbgprintf("\n");
		}

		FreeMem(buffer, READ_32MB_SIZE);
		return -100;  // Verification error
	}

	dbgprintf("*** VERIFICATION PASSED ***\n");
	dbgprintf("All 32MB verified successfully!\n");

	dbgprintf("\nFirst 256 bytes of data:\n");
	for (ULONG i = 0; i < 256; i += 16) {
		dbgprintf("%08lx: ", (ULONG)i);
		for (ULONG j = 0; j < 16; j++) {
			dbgprintf("%02lx ", (ULONG)buffer[i + j]);
		}
		dbgprintf("\n");
	}

	FreeMem(buffer, READ_32MB_SIZE);
	dbgprintf("\nBuffer freed.\n\n");

	return 0;
}

/*
 * Generate a 32MB file with pseudo-random data
 * File can be written to disk manually
 */
LONG
DoGenerateFile(const char *filename)
{
	UBYTE *buffer;
	BPTR fh;
	ULONG bytes_written;

	dbgprintf("\n=== Generating 32MB Random File ===\n");
	dbgprintf("Filename: %s\n", filename);
	dbgprintf("Size: %ld bytes (32 MB)\n", READ_32MB_SIZE);
	dbgprintf("Pattern: PRNG (seed 0x12345678)\n\n");

	// Allocate 32MB buffer
	dbgprintf("Allocating 32MB buffer...\n");
	buffer = AllocMem(READ_32MB_SIZE, MEMF_FAST);
	if (!buffer) {
		dbgprintf("ERROR: Could not allocate 32MB FAST memory\n");
		dbgprintf("Trying CHIP memory...\n");
		buffer = AllocMem(READ_32MB_SIZE, MEMF_CHIP);
		if (!buffer) {
			dbgprintf("ERROR: Could not allocate 32MB memory\n");
			return -1;
		}
	}

	dbgprintf("Buffer allocated at: 0x%08lx\n", (ULONG)buffer);

	// Fill with random data
	dbgprintf("Generating random data...\n");
	ResetRandom();  // Start with known seed
	FillRandomData(buffer, READ_32MB_SIZE);
	dbgprintf("Data generated.\n");

	// Open file for writing
	dbgprintf("Opening file for writing...\n");
	fh = Open((STRPTR)filename, MODE_NEWFILE);
	if (!fh) {
		dbgprintf("ERROR: Could not create file '%s'\n", filename);
		FreeMem(buffer, READ_32MB_SIZE);
		return -2;
	}

	// Write data
	dbgprintf("Writing 32MB to file...\n");
	bytes_written = Write(fh, buffer, READ_32MB_SIZE);
	Close(fh);

	if (bytes_written != READ_32MB_SIZE) {
		dbgprintf("ERROR: Write failed (wrote %ld / %ld bytes)\n",
		          bytes_written, READ_32MB_SIZE);
		FreeMem(buffer, READ_32MB_SIZE);
		return -3;
	}

	dbgprintf("\n=== File Generated Successfully ===\n");
	dbgprintf("Wrote: %ld bytes (32 MB)\n", bytes_written);
	dbgprintf("File: %s\n", filename);

	// Show first 256 bytes
	dbgprintf("\nFirst 256 bytes of data:\n");
	for (ULONG i = 0; i < 256; i += 16) {
		dbgprintf("%08lx: ", (ULONG)i);
		for (ULONG j = 0; j < 16; j++) {
			dbgprintf("%02lx ", (ULONG)buffer[i + j]);
		}
		dbgprintf("\n");
	}

	FreeMem(buffer, READ_32MB_SIZE);
	dbgprintf("\nBuffer freed.\n");
	dbgprintf("\nNOTE: You can now write this file to SCSI disk using:\n");
	dbgprintf("      dd if=%s of=/dev/sdi bs=512\n", filename);
	dbgprintf("      (or use Amiga file copy tool)\n\n");

	return 0;
}
