/*
 * SCSI Protocol Operations
 * Handles SCSI commands independently of NCR hardware
 */

#include "scsi.h"
#include "ncr_dmatest.h"
#include "dprintf.h"
#include <stdio.h>
#include <string.h>
#include <exec/memory.h>
#include <proto/exec.h>

/* Use dbgprintf for dual output (console + debug) */
#define printf dbgprintf

/*
 * Data structure for SCSI operations
 * Contains all buffers needed for a SCSI command
 */
struct scsi_operation {
	/* Message buffers */
	UBYTE msg_out[8];           /* Messages to target */
	UBYTE msg_in[8];            /* Messages from target */

	/* Command buffer */
	UBYTE command[12];          /* SCSI command (max 12 bytes) */
	UBYTE command_len;          /* Actual command length */

	/* Status buffer */
	UBYTE status;               /* SCSI status byte */

	/* Data buffer */
	UBYTE *data;                /* Data buffer (if needed) */
	ULONG data_len;             /* Data length */
	UBYTE data_direction;       /* 0=out, 1=in */
};

/*
 * Build a SCRIPTS program for TEST_UNIT_READY
 * This is the simplest SCSI command - no data phase
 *
 * Phase sequence:
 * 1. SELECT ATN target
 * 2. MSG_OUT: send IDENTIFY message
 * 3. CMD: send TEST_UNIT_READY command (6 bytes)
 * 4. STATUS: read status byte
 * 5. MSG_IN: read COMMAND_COMPLETE message
 * 6. DISCONNECT
 *
 * Returns: Pointer to SCRIPTS program in allocated memory
 */
static ULONG* BuildTestUnitReadyScript(UBYTE target_id, UBYTE lun,
                                       struct scsi_operation *op,
                                       volatile struct ncr710 *ncr)
{
	ULONG *script;
	ULONG *p;

	/* Allocate script buffer - need about 64 longwords */
	script = (ULONG *)AllocMem(256, MEMF_FAST | MEMF_CLEAR);
	if (!script) {
		printf("ERROR: Could not allocate SCRIPTS buffer\n");
		return NULL;
	}

	p = script;

	/* Prepare IDENTIFY message (0x80 = IDENTIFY, no disconnect) */
	op->msg_out[0] = SCSI_MSG_IDENTIFY | (lun & 0x07);

	/* Prepare TEST_UNIT_READY command */
	memset(op->command, 0, 6);
	op->command[0] = SCSI_TEST_UNIT_READY;
	op->command_len = 6;

	/*
	 * Instruction 0: SELECT with ATN and RESELECT
	 * Opcode: 0x47 (SELECT with ATN and RESELECT, relative addressing)
	 * Bits 0-7: Target ID bitmap
	 * Second word: relative jump offset on selection failure
	 */
	*p++ = 0x47000000 | (1 << target_id);  /* SELECT ATN+RESEL target_id */
	*p++ = 0x00000000;                      /* Offset filled later */

	/*
	 * Instruction 2: MOVE 1 byte from msg_out, WHEN MSG_OUT
	 * Send IDENTIFY message with LUN
	 */
	*p++ = 0x0E000001 | (SCSI_PHASE_MSG_OUT << 24);
	*p++ = (ULONG)&op->msg_out[0];

	/*
	 * Instruction 3: MOVE 6 bytes from command, WHEN CMD
	 */
	*p++ = 0x0E000006 | (SCSI_PHASE_COMMAND << 24);
	*p++ = (ULONG)&op->command[0];

	/*
	 * Instruction 4: MOVE 1 byte to status, WHEN STATUS
	 */
	*p++ = 0x0E000001 | (SCSI_PHASE_STATUS << 24);
	*p++ = (ULONG)&op->status;

	/*
	 * Instruction 5: MOVE 1 byte to msg_in, WHEN MSG_IN
	 */
	*p++ = 0x0E000001 | (SCSI_PHASE_MSG_IN << 24);
	*p++ = (ULONG)&op->msg_in[0];

	/*
	 * Instruction 6: WAIT DISCONNECT
	 */
	*p++ = 0x48000000;
	*p++ = 0x00000000;

	/*
	 * Instruction 7: INT (signal completion)
	 */
	*p++ = 0x98080000;
	*p++ = 0xFEED0000;

	/*
	 * Instruction 8: selection_failed label
	 */
	*p++ = 0x98080000;
	*p++ = 0xDEAD0000;

	/* Calculate address for SELECT failure jump (instruction 0) */
	/* Instruction 7 (selection_failed) is at offset 7 * 8 = 56 bytes from script start */
	/* SELECT wants absolute address, not relative offset */
	script[1] = (ULONG)script + 56;  /* script[1] is the second word of instruction 0 */

	return script;
}

/*
 * Execute TEST_UNIT_READY command
 * This is the simplest SCSI command - just checks if device is ready
 *
 * Returns:
 *   SCSI_OK = Device ready
 *   SCSI_ERR_SELECTION = No device at this ID
 *   SCSI_ERR_CHECK = Device returned CHECK CONDITION
 *   SCSI_ERR_TIMEOUT = Command timed out
 */
LONG SCSI_TestUnitReady(volatile struct ncr710 *ncr, UBYTE target_id, UBYTE lun)
{
	struct scsi_operation *op;
	ULONG *script;
	ULONG timeout;
	UBYTE istat, dstat, sstat0;
	LONG result = SCSI_ERR_TIMEOUT;

	printf("SCSI: Testing device %ld:%ld...\n", (ULONG)target_id, (ULONG)lun);

	/* Allocate operation structure */
	op = (struct scsi_operation *)AllocMem(sizeof(struct scsi_operation),
	                                        MEMF_CLEAR);
	if (!op) {
		printf("ERROR: Could not allocate operation structure\n");
		return SCSI_ERR_TIMEOUT;
	}

	/* Build SCRIPTS program */
	script = BuildTestUnitReadyScript(target_id, lun, op, ncr);
	if (!script) {
		FreeMem(op, sizeof(struct scsi_operation));
		return SCSI_ERR_TIMEOUT;
	}

	/* Flush caches */
	CacheClearU();

	/* Clear any pending interrupts */
	(void)ncr->istat;
	(void)ncr->dstat;
	(void)ncr->sstat0;

	/* Start the SCRIPTS program */
	printf("  Starting SCRIPTS execution...\n");
	printf("  Script address: 0x%08lx\n", (ULONG)script);
	printf("  Script instruction 0: 0x%08lx 0x%08lx\n", script[0], script[1]);

	/* Verify NCR registers before starting */
	printf("  Pre-start ISTAT: 0x%02lx DSTAT: 0x%02lx SSTAT0: 0x%02lx\n",
	       (ULONG)ncr->istat, (ULONG)ncr->dstat, (ULONG)ncr->sstat0);

	WRITE_LONG(ncr, dsp, (ULONG)script);

	/* Read back DSP to verify it was set */
	printf("  DSP after write: 0x%08lx\n", ncr->dsp);

	/* Wait for completion */
	for (timeout = 0; timeout < 1000000; timeout++) {
		istat = ncr->istat;

		/* Check for DMA interrupt */
		if (istat & ISTATF_DIP) {
			dstat = ncr->dstat;
			printf("  DMA interrupt detected: DSTAT=0x%02lx DSP=0x%08lx\n",
			       (ULONG)dstat, ncr->dsp);

			/* Check for script interrupt (completion signal) */
			if (dstat & DSTATF_SIR) {
				ULONG dsps = ncr->dsps;

				if (dsps == 0xFEED0000) {
					/* Success! Check status */
					printf("  Command completed\n");
					printf("  Status: 0x%02lx\n", (ULONG)op->status);
					printf("  Message: 0x%02lx\n", (ULONG)op->msg_in[0]);

					if (op->status == SCSI_STATUS_GOOD) {
						result = SCSI_OK;
					} else if (op->status == SCSI_STATUS_CHECK) {
						result = SCSI_ERR_CHECK;
					} else {
						result = SCSI_ERR_TIMEOUT;
					}
					break;
				} else if (dsps == 0xDEAD0000) {
					/* Selection failed */
					printf("  Selection failed - no device at ID %ld\n",
					        (ULONG)target_id);
					result = SCSI_ERR_SELECTION;
					break;
				}
			}

			/* Check for errors */
			if (dstat & DSTATF_IID) {
				printf("ERROR: Illegal instruction in SCRIPTS\n");
				printf("  DSTAT: 0x%02lx\n", (ULONG)dstat);
				printf("  DSP: 0x%08lx\n", ncr->dsp);
				result = SCSI_ERR_TIMEOUT;
				break;
			}
		}

		/* Check for SCSI interrupt (phase mismatch, etc.) */
		if (istat & ISTATF_SIP) {
			sstat0 = ncr->sstat0;
			printf("WARNING: SCSI interrupt (SSTAT0=0x%02lx DSP=0x%08lx)\n",
			       (ULONG)sstat0, ncr->dsp);

			/* Selection timeout? (bit 5 = 0x20) */
			if (sstat0 & 0x20) {
				printf("  Selection timeout - no device\n");
				result = SCSI_ERR_SELECTION;
				break;
			}

			/* Unexpected disconnect? (bit 2 = 0x04) */
			if (sstat0 & 0x04) {
				printf("  Unexpected disconnect\n");
				result = SCSI_ERR_DISCONNECT;
				break;
			}

			/* SCSI Gross Error? (bit 3 = 0x08) */
			if (sstat0 & 0x08) {
				printf("  SCSI Gross Error\n");
				result = SCSI_ERR_PHASE;
				break;
			}
		}

		/* Small delay every 256 iterations */
		if ((timeout & 0xFF) == 0) {
			/* Just spin for now */
		}
	}

	if (timeout >= 1000000) {
		printf("ERROR: Command timeout\n");
		printf("  ISTAT: 0x%02lx\n", (ULONG)ncr->istat);
		printf("  DSTAT: 0x%02lx\n", (ULONG)ncr->dstat);
		printf("  DSP: 0x%08lx\n", ncr->dsp);
	}

	/* Cleanup */
	FreeMem(script, 256);
	FreeMem(op, sizeof(struct scsi_operation));

	return result;
}

/*
 * Build a SCRIPTS program for READ(6) command
 * Reads one or more 512-byte sectors from disk
 *
 * Phase sequence:
 * 1. SELECT ATN target
 * 2. MSG_OUT: send IDENTIFY message
 * 3. CMD: send READ(6) command
 * 4. DATA_IN: read sector data
 * 5. STATUS: read status byte
 * 6. MSG_IN: read COMMAND_COMPLETE message
 * 7. DISCONNECT
 */
static ULONG* BuildRead6Script(UBYTE target_id, UBYTE lun,
                               ULONG lba, UBYTE num_blocks,
                               struct scsi_operation *op,
                               volatile struct ncr710 *ncr)
{
	ULONG *script;
	ULONG *p;
	struct scsi_read6_cmd *cmd;

	/* Allocate script buffer */
	script = (ULONG *)AllocMem(256, MEMF_FAST | MEMF_CLEAR);
	if (!script) {
		printf("ERROR: Could not allocate SCRIPTS buffer\n");
		return NULL;
	}

	p = script;

	/* Prepare IDENTIFY message */
	op->msg_out[0] = SCSI_MSG_IDENTIFY | (lun & 0x07);

	/* Prepare READ(6) command */
	cmd = (struct scsi_read6_cmd *)op->command;
	cmd->opcode = SCSI_READ6;
	cmd->lba_high = ((lba >> 16) & 0x1F) | ((lun & 0x07) << 5);
	cmd->lba_mid = (lba >> 8) & 0xFF;
	cmd->lba_low = lba & 0xFF;
	cmd->length = num_blocks;
	cmd->control = 0;
	op->command_len = 6;

	/*
	 * Instruction 0: SELECT with ATN and RESELECT
	 * Opcode: 0x47 (matching ROM driver)
	 */
	*p++ = 0x47000000 | (1 << target_id);
	*p++ = 0x00000000;  /* Offset filled later */

	/* MOVE 1 byte from msg_out, WHEN MSG_OUT */
	*p++ = 0x0E000001 | (SCSI_PHASE_MSG_OUT << 24);
	*p++ = (ULONG)&op->msg_out[0];

	/* MOVE 6 bytes from command, WHEN CMD */
	*p++ = 0x0E000006 | (SCSI_PHASE_COMMAND << 24);
	*p++ = (ULONG)&op->command[0];

	/* MOVE data_len bytes to data buffer, WHEN DATA_IN */
	*p++ = 0x0E000000 | (SCSI_PHASE_DATA_IN << 24) | op->data_len;
	*p++ = (ULONG)op->data;

	/* MOVE 1 byte to status, WHEN STATUS */
	*p++ = 0x0E000001 | (SCSI_PHASE_STATUS << 24);
	*p++ = (ULONG)&op->status;

	/* MOVE 1 byte to msg_in, WHEN MSG_IN */
	*p++ = 0x0E000001 | (SCSI_PHASE_MSG_IN << 24);
	*p++ = (ULONG)&op->msg_in[0];

	/* WAIT DISCONNECT */
	*p++ = 0x48000000;
	*p++ = 0x00000000;

	/* INT (signal completion) */
	*p++ = 0x98080000;
	*p++ = 0xFEED0000;

	/* selection_failed: */
	*p++ = 0x98080000;
	*p++ = 0xDEAD0000;

	/* Calculate address for SELECT failure jump (instruction 0) */
	/* Instruction 8 (selection_failed) is at offset 8 * 8 = 64 bytes from script start */
	/* SELECT wants absolute address, not relative offset */
	script[1] = (ULONG)script + 64;  /* script[1] is second word of SELECT instruction */

	return script;
}

/*
 * Read sectors from SCSI disk using READ(6) command
 *
 * Parameters:
 *   ncr - NCR chip pointer
 *   target_id - SCSI ID (0-7)
 *   lun - Logical Unit Number (usually 0)
 *   lba - Logical Block Address (sector number)
 *   num_blocks - Number of 512-byte sectors to read (1-255)
 *   buffer - Buffer to read into (must be at least num_blocks * 512 bytes)
 *
 * Returns:
 *   SCSI_OK on success
 *   Negative error code on failure
 */
LONG SCSI_Read6(volatile struct ncr710 *ncr, UBYTE target_id, UBYTE lun,
                ULONG lba, UBYTE num_blocks, UBYTE *buffer)
{
	struct scsi_operation *op;
	ULONG *script;
	ULONG timeout;
	UBYTE istat, dstat, sstat0;
	LONG result = SCSI_ERR_TIMEOUT;
	ULONG data_len = num_blocks * 512;

	printf("SCSI: Reading %ld block(s) from ID %ld LBA %ld...\n",
	       (ULONG)num_blocks, (ULONG)target_id, lba);

	/* Allocate operation structure */
	op = (struct scsi_operation *)AllocMem(sizeof(struct scsi_operation),
	                                        MEMF_CLEAR);
	if (!op) {
		printf("ERROR: Could not allocate operation structure\n");
		return SCSI_ERR_TIMEOUT;
	}

	/* Set data buffer information */
	op->data = buffer;
	op->data_len = data_len;
	op->data_direction = 1;  /* IN */

	/* Build SCRIPTS program */
	script = BuildRead6Script(target_id, lun, lba, num_blocks, op, ncr);
	if (!script) {
		FreeMem(op, sizeof(struct scsi_operation));
		return SCSI_ERR_TIMEOUT;
	}

	/* Flush caches */
	CacheClearU();

	/* Clear any pending interrupts */
	(void)ncr->istat;
	(void)ncr->dstat;
	(void)ncr->sstat0;

	/* Start the SCRIPTS program */
	printf("  Starting SCRIPTS execution...\n");
	WRITE_LONG(ncr, dsp, (ULONG)script);

	/* Wait for completion */
	for (timeout = 0; timeout < 5000000; timeout++) {
		istat = ncr->istat;

		/* Check for DMA interrupt */
		if (istat & ISTATF_DIP) {
			dstat = ncr->dstat;

			/* Check for script interrupt (completion signal) */
			if (dstat & DSTATF_SIR) {
				ULONG dsps = ncr->dsps;

				if (dsps == 0xFEED0000) {
					/* Success! Check status */
					printf("  Command completed\n");
					printf("  Status: 0x%02lx\n", (ULONG)op->status);

					/* Flush caches to ensure we see the data */
					CacheClearU();

					if (op->status == SCSI_STATUS_GOOD) {
						result = SCSI_OK;
					} else if (op->status == SCSI_STATUS_CHECK) {
						result = SCSI_ERR_CHECK;
					} else {
						result = SCSI_ERR_TIMEOUT;
					}
					break;
				} else if (dsps == 0xDEAD0000) {
					/* Selection failed */
					printf("  Selection failed - no device at ID %ld\n",
					        (ULONG)target_id);
					result = SCSI_ERR_SELECTION;
					break;
				}
			}

			/* Check for errors */
			if (dstat & DSTATF_IID) {
				printf("ERROR: Illegal instruction in SCRIPTS\n");
				printf("  DSTAT: 0x%02lx\n", (ULONG)dstat);
				printf("  DSP: 0x%08lx\n", ncr->dsp);
				result = SCSI_ERR_TIMEOUT;
				break;
			}
		}

		/* Check for SCSI interrupt */
		if (istat & ISTATF_SIP) {
			sstat0 = ncr->sstat0;
			printf("WARNING: SCSI interrupt (SSTAT0=0x%02lx)\n", (ULONG)sstat0);

			/* Selection timeout? (bit 5 = 0x20) */
			if (sstat0 & 0x20) {
				printf("  Selection timeout - no device\n");
				result = SCSI_ERR_SELECTION;
				break;
			}

			/* Unexpected disconnect? (bit 2 = 0x04) */
			if (sstat0 & 0x04) {
				printf("  Unexpected disconnect\n");
				result = SCSI_ERR_DISCONNECT;
				break;
			}

			/* SCSI Gross Error? (bit 3 = 0x08) */
			if (sstat0 & 0x08) {
				printf("  SCSI Gross Error\n");
				result = SCSI_ERR_PHASE;
				break;
			}
		}

		/* Small delay every 256 iterations */
		if ((timeout & 0xFF) == 0) {
			/* Just spin */
		}
	}

	if (timeout >= 5000000) {
		printf("ERROR: Command timeout\n");
		printf("  ISTAT: 0x%02lx\n", (ULONG)ncr->istat);
		printf("  DSTAT: 0x%02lx\n", (ULONG)ncr->dstat);
		printf("  DSP: 0x%08lx\n", ncr->dsp);
	}

	/* Cleanup */
	FreeMem(script, 256);
	FreeMem(op, sizeof(struct scsi_operation));

	return result;
}

/*
 * Scan SCSI bus for devices
 * Tests each ID from 0-7 (except host ID)
 * Returns the first device ID found, or -1 if none
 */
LONG SCSI_ScanBus(volatile struct ncr710 *ncr, UBYTE host_id)
{
	UBYTE id;
	LONG result;
	LONG first_device = -1;

	printf("\n=== Scanning SCSI Bus ===\n");
	printf("Host ID: %ld\n", (ULONG)host_id);

	for (id = 0; id < 8; id++) {
		/* Skip host ID */
		if (id == host_id)
			continue;

		printf("\nID %ld: ", (ULONG)id);

		result = SCSI_TestUnitReady(ncr, id, 0);

		if (result == SCSI_OK) {
			printf("  Device found and ready\n");
			if (first_device == -1)
				first_device = id;
		} else if (result == SCSI_ERR_SELECTION) {
			printf("  No device\n");
		} else if (result == SCSI_ERR_CHECK) {
			printf("  Device found but not ready (CHECK CONDITION)\n");
			if (first_device == -1)
				first_device = id;
		} else {
			printf("  Error: %ld\n", result);
		}
	}

	printf("\n=== Scan Complete ===\n");
	return first_device;
}
