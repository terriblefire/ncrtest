/*
 * ncr_scsi_main.c - NCR 53C710 SCSI Command Tool
 * Command-line interface for SCSI operations
 */

#include "ncr_scsi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proto/exec.h>

extern LONG InitNCR(volatile struct ncr710 *ncr);
extern void dbgprintf(const char *format, ...);

#define VERSION_STRING "NCR SCSI Tool v1.0"
#define NCR_ADDRESS 0x00DD0040

static void
print_usage(void)
{
	dbgprintf("\n%s\n", VERSION_STRING);
	dbgprintf("Usage: ncr_scsi <command> [options]\n\n");
	dbgprintf("Commands:\n");
	dbgprintf("  inquiry <id>              - Send INQUIRY to SCSI ID (0-7)\n");
	dbgprintf("  read <id>                 - Read first 32MB from disk at SCSI ID (0-7)\n");
	dbgprintf("\n");
	dbgprintf("Examples:\n");
	dbgprintf("  ncr_scsi inquiry 3        - Query device at SCSI ID 3\n");
	dbgprintf("  ncr_scsi read 3           - Read 32MB from disk at SCSI ID 3\n");
	dbgprintf("\n");
}

int
main(int argc, char **argv)
{
	volatile struct ncr710 *ncr;
	struct InquiryData *inq_data;
	UBYTE target_id;
	LONG result;

	dbgprintf("\n%s\n", VERSION_STRING);
	dbgprintf("==============================\n\n");

	// Check arguments
	if (argc < 2) {
		print_usage();
		return 1;
	}

	// Get NCR chip pointer
	ncr = (volatile struct ncr710 *)NCR_ADDRESS;
	dbgprintf("NCR chip at: 0x%08lx\n", (ULONG)ncr);

	// Initialize NCR chip (basic DMA setup)
	dbgprintf("Initializing NCR 53C710...\n");
	if (InitNCR(ncr) < 0) {
		dbgprintf("FATAL: NCR initialization failed\n");
		return 1;
	}
	dbgprintf("NCR initialized successfully\n\n");

	// Enable SCSI bus operations
	if (InitNCRForSCSI(ncr) < 0) {
		dbgprintf("FATAL: SCSI bus initialization failed\n");
		return 1;
	}

	// Parse command
	if (strcmp(argv[1], "inquiry") == 0) {
		// INQUIRY command
		if (argc < 3) {
			dbgprintf("ERROR: Missing SCSI ID\n");
			dbgprintf("Usage: ncr_scsi inquiry <id>\n");
			return 1;
		}

		target_id = atoi(argv[2]);
		if (target_id > 7) {
			dbgprintf("ERROR: Invalid SCSI ID %ld (must be 0-7)\n",
			          (ULONG)target_id);
			return 1;
		}

		// Allocate buffer for INQUIRY data
		inq_data = AllocMem(sizeof(struct InquiryData), MEMF_FAST | MEMF_CLEAR);
		if (!inq_data) {
			dbgprintf("ERROR: Could not allocate INQUIRY buffer\n");
			return 1;
		}

		// Execute INQUIRY
		result = DoInquiry(ncr, target_id, inq_data);

		if (result == 0) {
			// Success - print results
			PrintInquiryData(inq_data);
		} else {
			dbgprintf("\nINQUIRY failed with error code %ld\n", result);
		}

		FreeMem(inq_data, sizeof(struct InquiryData));

		return (result == 0) ? 0 : 1;

	} else if (strcmp(argv[1], "read") == 0) {
		// READ command
		if (argc < 3) {
			dbgprintf("ERROR: Missing SCSI ID\n");
			dbgprintf("Usage: ncr_scsi read <id>\n");
			return 1;
		}

		target_id = atoi(argv[2]);
		if (target_id > 7) {
			dbgprintf("ERROR: Invalid SCSI ID %ld (must be 0-7)\n",
			          (ULONG)target_id);
			return 1;
		}

		// Execute READ 32MB
		result = DoRead32MB(ncr, target_id);

		if (result != 0) {
			dbgprintf("\nREAD failed with error code %ld\n", result);
		}

		return (result == 0) ? 0 : 1;

	} else {
		dbgprintf("ERROR: Unknown command '%s'\n", argv[1]);
		print_usage();
		return 1;
	}

	return 0;
}
