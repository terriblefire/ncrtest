/*
 * ncr_scsi.c - SCSI INQUIRY Command Implementation
 * Based on Kickstart ROM NCR 53C710 driver
 */

#include "ncr_scsi.h"
#include <stdio.h>
#include <string.h>
#include <exec/execbase.h>
#include <proto/exec.h>

/* External functions from ncr_init.c */
extern LONG InitNCR(volatile struct ncr710 *ncr);
extern void kprintf(char *,...);
extern void dbgprintf(const char *format, ...);

/* NCR chip address (A4000T) */
#define NCR_ADDRESS 0x00DD0040

/* Write offset for longword writes */
#define NCR_WRITE_OFFSET 0x80

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
	ULONG timeout;
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

	// Start SCRIPTS execution (load DSP)
	WRITE_LONG(ncr, dsp, (ULONG)inquiry_script);

	// Wait for completion (like ROM driver's interrupt handler)
	for (timeout = 0; timeout < 1000000; timeout++) {
		istat = ncr->istat;

		// Check for DMA interrupt
		if (istat & ISTATF_DIP) {
			dstat = ncr->dstat;

			// Check for SCRIPTS interrupt
			if (dstat & DSTATF_SIR) {
				ULONG dsps = ncr->dsps;

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
					break;
				} else if (dsps == 0xBADBAD00) {
					dbgprintf("ERROR: Selection failed\n");
					result = -3;
					break;
				} else {
					dbgprintf("ERROR: Unexpected interrupt (0x%08lx)\n", dsps);
					result = -4;
					break;
				}
			}

			// Check for errors
			if (dstat & (DSTATF_IID | DSTATF_ABRT | DSTATF_SSI)) {
				dbgprintf("ERROR: DMA error (DSTAT=0x%02lx)\n", (ULONG)dstat);
				result = -5;
				break;
			}
		}

		// Check for SCSI interrupt
		if (istat & ISTATF_SIP) {
			UBYTE sstat0 = ncr->sstat0;
			dbgprintf("ERROR: SCSI interrupt (SSTAT0=0x%02lx)\n", (ULONG)sstat0);
			result = -6;
			break;
		}
	}

	if (timeout >= 1000000) {
		dbgprintf("ERROR: Timeout waiting for completion\n");
		dbgprintf("  ISTAT: 0x%02lx\n", (ULONG)ncr->istat);
		dbgprintf("  DSTAT: 0x%02lx\n", (ULONG)ncr->dstat);
		dbgprintf("  DSP:   0x%08lx\n", ncr->dsp);
		result = -7;
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
	ULONG timeout;
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

	// Start SCRIPTS execution (reuse same SCRIPTS as INQUIRY)
	WRITE_LONG(ncr, dsp, (ULONG)inquiry_script);

	// Wait for completion
	for (timeout = 0; timeout < 5000000; timeout++) {
		istat = ncr->istat;

		// Check for DMA interrupt
		if (istat & ISTATF_DIP) {
			dstat = ncr->dstat;

			// Check for SCRIPTS interrupt
			if (dstat & DSTATF_SIR) {
				ULONG dsps = ncr->dsps;

				if (dsps == 0xDEADBEEF) {
					// Success!
					if (dsa->status_buf[0] == SCSI_GOOD) {
						result = 0;
					} else {
						dbgprintf("ERROR: Bad status (0x%02lx)\n",
						          (ULONG)dsa->status_buf[0]);
						result = -2;
					}
					break;
				} else if (dsps == 0xBADBAD00) {
					dbgprintf("ERROR: Selection failed\n");
					result = -3;
					break;
				} else {
					dbgprintf("ERROR: Unexpected interrupt (0x%08lx)\n", dsps);
					result = -4;
					break;
				}
			}

			// Check for errors
			if (dstat & (DSTATF_IID | DSTATF_ABRT | DSTATF_SSI)) {
				dbgprintf("ERROR: DMA error (DSTAT=0x%02lx)\n", (ULONG)dstat);
				result = -5;
				break;
			}
		}

		// Check for SCSI interrupt
		if (istat & ISTATF_SIP) {
			UBYTE sstat0 = ncr->sstat0;
			dbgprintf("ERROR: SCSI interrupt (SSTAT0=0x%02lx)\n", (ULONG)sstat0);
			result = -6;
			break;
		}
	}

	if (timeout >= 5000000) {
		dbgprintf("ERROR: Timeout waiting for completion\n");
		result = -7;
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

	dbgprintf("\nFirst 256 bytes of data:\n");
	for (ULONG i = 0; i < 256; i += 16) {
		dbgprintf("%08lx: ", (ULONG)i);
		for (ULONG j = 0; j < 16; j++) {
			dbgprintf("%02lx ", (ULONG)buffer[i + j]);
		}
		dbgprintf("\n");
	}

	dbgprintf("\nNOTE: Buffer not freed - data remains in memory at 0x%08lx\n", (ULONG)buffer);
	dbgprintf("      Use Avail command to see memory usage\n\n");

	return 0;
}
