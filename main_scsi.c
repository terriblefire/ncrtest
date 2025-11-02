/*
 * SCSI Test Main - Tests SCSI bus operations
 */

#include "ncr_dmatest.h"
#include "scsi.h"
#include "dprintf.h"
#include <stdio.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <proto/exec.h>

/* Use dbgprintf for dual output (console + debug) */
#define printf dbgprintf

/* External SysBase from crt0 */
extern struct ExecBase *SysBase;

int main(void)
{
	volatile struct ncr710 *ncr;
	LONG first_device;
	UBYTE *buffer;
	LONG result;
	int i;

	printf("\n");
	printf("===========================================\n");
	printf("%s\n", VERSION_STRING);
	printf("SCSI Disk Read Test Tool\n");
	printf("===========================================\n\n");

	ncr = (volatile struct ncr710 *)NCR_ADDRESS;
	printf("NCR chip at: 0x%08lx\n\n", NCR_ADDRESS);

	/* Initialize NCR for SCSI operations (host ID = 7) */
	if (InitNCRForSCSI(ncr, 7) < 0) {
		printf("\nFATAL: NCR SCSI initialization failed\n");
		return 20;
	}

	printf("\n");

	/* Install interrupt handler for better diagnostics */
	if (InstallNCRInterrupt(ncr) < 0) {
		printf("\nWARNING: Could not install interrupt handler\n");
		printf("Continuing with polling mode...\n");
	}

	printf("\n");

	/* Scan SCSI bus for devices */
	first_device = SCSI_ScanBus(ncr, 7);

	if (first_device < 0) {
		printf("\nNo SCSI devices found!\n");

		/* Print interrupt statistics */
		ULONG total, dma, scsi;
		GetNCRInterruptStats(&total, &dma, &scsi);
		printf("\n=== Interrupt Statistics ===\n");
		printf("Total interrupts: %ld\n", total);
		printf("DMA interrupts:   %ld\n", dma);
		printf("SCSI interrupts:  %ld\n", scsi);

		/* Remove interrupt handler */
		RemoveNCRInterrupt();

		return 10;
	}

	printf("\nUsing first device at ID %ld\n", first_device);

	/* Allocate a buffer for reading (one sector = 512 bytes) */
	buffer = AllocMem(512, MEMF_ANY | MEMF_CLEAR);
	if (!buffer) {
		printf("ERROR: Could not allocate read buffer\n");
		return 20;
	}

	printf("\n=== Reading Sector 0 (Boot Sector) ===\n");

	/* Read sector 0 from the first SCSI disk */
	result = SCSI_Read6(ncr, first_device, 0, 0, 1, buffer);

	if (result == SCSI_OK) {
		printf("\n✓ Successfully read sector 0!\n\n");

		/* Display first 256 bytes as hex dump */
		printf("First 256 bytes:\n");
		for (i = 0; i < 256; i++) {
			if ((i % 16) == 0)
				printf("%04x: ", i);

			printf("%02x ", buffer[i]);

			if ((i % 16) == 15)
				printf("\n");
		}
		printf("\n");

		/* Check for common boot sector signatures */
		if (buffer[0] == 0x00 && buffer[1] == 0x00 && buffer[2] == 0x03 && buffer[3] == 0xF3) {
			printf("Looks like an Amiga OFS boot sector!\n");
		} else if (buffer[510] == 0x55 && buffer[511] == 0xAA) {
			printf("Looks like a PC/MBR boot sector!\n");
		} else {
			printf("Boot sector signature: %02x %02x %02x %02x\n",
			       buffer[0], buffer[1], buffer[2], buffer[3]);
		}
	} else {
		printf("\n✗ Failed to read sector 0 (error %ld)\n", result);
	}

	/* Free the buffer */
	FreeMem(buffer, 512);

	/* Print interrupt statistics */
	ULONG total, dma, scsi;
	GetNCRInterruptStats(&total, &dma, &scsi);
	printf("\n=== Interrupt Statistics ===\n");
	printf("Total interrupts: %ld\n", total);
	printf("DMA interrupts:   %ld\n", dma);
	printf("SCSI interrupts:  %ld\n", scsi);

	/* Remove interrupt handler */
	RemoveNCRInterrupt();

	printf("\nTest complete.\n");
	return (result == SCSI_OK) ? 0 : 10;
}
