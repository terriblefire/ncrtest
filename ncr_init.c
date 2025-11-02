/*
 * NCR 53C710 DMA Test Tool - Initialization and Reset
 */

#include "ncr_dmatest.h"
#include <stdio.h>
#include <exec/execbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <hardware/cia.h>

/*
 * CIA-based delay function (from ROM code)
 * Delays for approximately the specified number of microseconds
 */
static void poll_cia(ULONG microseconds)
{
	struct CIA *ciaa = (struct CIA *)0xBFE001;
	volatile UBYTE dummy;

	for (ULONG i = 0; i < microseconds; i++) {
		dummy = ciaa->ciatodlow;
		(void)dummy;
	}
}


/*
 * Check if NCR 53C710 chip is present
 * Returns 0 if present, -1 if not detected
 *
 * NOTE: Must be called from supervisor mode!
 *
 * On A4000T, we must configure GARY chipset and set DCNTL.EA before
 * any other access to the NCR chip, otherwise the bus will hang.
 */
LONG DetectNCR(volatile struct ncr710 *ncr)
{
	volatile UBYTE *gary = (volatile UBYTE *)0x00DE0000;
	UBYTE gary_timeout;
	UBYTE istat_before, istat_after;

	printf("Detecting NCR 53C710 chip...\n");

	// Following the A4000T ROM initialization sequence:
	// See kickstart/scsidisk/init.asm lines 294-307

	printf("  Configuring GARY chipset for NCR access...\n");
	Disable();

	// Set GARY to DSACK timeout mode (9us) instead of bus error
	*gary = 0x00;

	// Read GARY register to reset timeout bit
	gary_timeout = *gary;

	// Critical: Must set EA bit in DCNTL before any other NCR register access!
	// This links STERM and SLAC internally so the chip will respond
	printf("  Setting DCNTL.EA bit...\n");
	ncr->dcntl = DCNTLF_EA | DCNTLF_COM;

	// Check if GARY timed out (bit 7 set means timeout = no chip)
	gary_timeout = *gary;

	// Set GARY back to bus-error mode
	*gary = 0x80;

	Enable();

	// Check if we got a timeout
	if (gary_timeout & 0x80) {
		printf("ERROR: NCR chip not detected (GARY timeout)\n");
		printf("  The hardware may not be present\n");
		return -1;
	}

	printf("  GARY timeout check passed\n");

	// Now we can safely read ISTAT register
	printf("  Reading ISTAT register...\n");
	istat_before = ncr->istat;
	istat_after = ncr->istat;

	// If we read all 1's, the hardware probably isn't there
	if (istat_before == 0xFF && istat_after == 0xFF) {
		printf("WARNING: NCR chip not detected (bus reads 0xFF)\n");
		printf("  This may be an emulator without NCR hardware emulation\n");
		return -1;
	}

	printf("NCR chip detected (ISTAT=0x%02lx)\n", (ULONG)istat_before);
	return 0;
}

/*
 * Reset the NCR 53C710 chip
 * Based on init_chip() from kickstart/scsidisk/ncr.c
 */
LONG ResetNCR(volatile struct ncr710 *ncr)
{
	UBYTE istat;
	ULONG status;
	Disable();
	printf("Resetting NCR 53C710...\n");

	// Read initial ISTAT value
	istat = ncr->istat;
	printf("  ISTAT before reset: 0x%02lx\n", (ULONG)istat);

	// Abort any running scripts first (like ROM does)
	printf("  Aborting any running scripts...\n");
	ncr->istat |= ISTATF_ABRT;
	poll_cia(50000);  // 50ms delay

	// Software reset via ISTAT register
	printf("  Asserting software reset...\n");
	ncr->istat |= ISTATF_RST;

	// Clear ISTAT to 0 (like ROM does)
	ncr->istat = 0;
	poll_cia(100000);  // 100ms delay

	// CRITICAL: Disable burst bus mode immediately after reset (from ROM)
	// This must be done before accessing other registers
	printf("  Disabling burst bus mode...\n");
	ncr->ctest7 |= CTREST7_CDIS;

	// CRITICAL: Configure ctest0 - disable timer FIRST to avoid select timeouts
	// no byte-to-byte timer, enable active negation, filter REQ/ACK
	printf("  Configuring chip test register 0...\n");
	ncr->ctest0 = CTEST0F_BTD | CTEST0F_EAN | CTEST0F_ERF;

	printf("  NCR chip reset complete\n");

	// Clear any pending interrupts from reset/abort
	printf("  Clearing pending interrupts...\n");
	while ((istat = ncr->istat & (ISTATF_SIP|ISTATF_DIP)) != 0) {
		poll_cia(50000);
		// Read status registers to clear interrupts
		status = *((volatile ULONG *) &(ncr->sstat2));
		(void)status;
	}
	
	Enable();

	printf("NCR reset complete (ISTAT=0x%02lx)\n", (ULONG)ncr->istat);
	return 0;
}

/*
 * Initialize the NCR 53C710 for DMA testing
 * Based on init_chip() from kickstart/scsidisk/ncr.c
 * This sets up the chip but does NOT enable SCSI bus operations
 */
LONG InitNCR(volatile struct ncr710 *ncr)
{
	printf("Initializing NCR 53C710 for DMA testing...\n");

	// First detect if the chip is present
	if (DetectNCR(ncr) < 0)
		return -1;

	// Reset the chip
	if (ResetNCR(ncr) < 0)
		return -1;

	// Disable all SCSI interrupts - we don't want to trigger any SCSI bus activity
	printf("  Disabling SCSI interrupts...\n");
	ncr->sien = 0;

	// Clear any pending SCSI status
	(void)ncr->sstat0;
	(void)ncr->sstat1;
	(void)ncr->sstat2;

	// Configure DMA mode
	// BL1|BL0 = burst length (11 = 8 transfers, matching ROM)
	// FC2 = function code
	printf("  Configuring DMA mode...\n");
	ncr->dmode = DMODEF_BL1 | DMODEF_BL0 | DMODEF_FC2;

	// Configure DMA control
	// EA = Enable Ack (CRITICAL for A4000T - already set in DetectNCR)
	// COM = Compatibility mode (from ROM)
	printf("  Configuring DMA control...\n");
	ncr->dcntl = DCNTLF_EA | DCNTLF_COM;

	// DO NOT enable DMA interrupts - we use polling instead!
	// Enabling interrupts without an interrupt handler causes hangs.
	// The ROM code has a full interrupt handler, but we're just polling
	// in RunDMATest() by reading istat/dstat registers.
	printf("  Disabling DMA interrupts (using polling mode)...\n");
	ncr->dien = 0;

	// Clear any pending DMA status
	(void)ncr->dstat;

	// Clear scratch registers
	WRITE_LONG(ncr, scratch, 0);
	WRITE_LONG(ncr, temp, 0);

	// Disable SCSI chip ID and control - we're not doing SCSI operations
	printf("  Disabling SCSI bus operations...\n");
	ncr->scid = 0;
	ncr->scntl0 = 0;
	ncr->scntl1 = 0;

	// Clear SCSI transfer register
	ncr->sxfer = 0;

	printf("NCR initialization complete\n");
	printf("  DMODE:  0x%02lx\n", (ULONG)ncr->dmode);
	printf("  DCNTL:  0x%02lx\n", (ULONG)ncr->dcntl);
	printf("  DIEN:   0x%02lx (interrupts disabled)\n", (ULONG)ncr->dien);

	return 0;
}

/*
 * Check for and handle any NCR interrupts/errors
 * Returns: 0 if OK, negative if error
 */
LONG CheckNCRStatus(volatile struct ncr710 *ncr, const char *context)
{
	UBYTE istat, dstat;

	istat = ncr->istat;

	// Check for DMA interrupt
	if (istat & ISTATF_DIP) {
		dstat = ncr->dstat;

		// Check for various error conditions
		if (dstat & DSTATF_IID) {
			printf("ERROR [%s]: Illegal Instruction Detected (DSTAT=0x%02lx)\n",
			        context, (ULONG)dstat);
			printf("  DSP: 0x%08lx\n", ncr->dsp);
			return -1;
		}

		if (dstat & DSTATF_ABRT) {
			printf("ERROR [%s]: Script Aborted (DSTAT=0x%02lx)\n",
			        context, (ULONG)dstat);
			return -1;
		}

		if (dstat & DSTATF_WTD) {
			printf("ERROR [%s]: Watchdog Timer Expired (DSTAT=0x%02lx)\n",
			        context, (ULONG)dstat);
			return -1;
		}

		// SSI (Single Step Interrupt) is expected if we're single-stepping
		if (dstat & DSTATF_SSI) {
			// This is OK for single-step mode
		}

		// SIR (Script Interrupt) is used to signal completion
		if (dstat & DSTATF_SIR) {
			// This is normal - script signaled completion
			return 0;
		}
	}

	// Check for SCSI interrupt (shouldn't happen in our tests)
	if (istat & ISTATF_SIP) {
		printf("WARNING [%s]: Unexpected SCSI interrupt (ISTAT=0x%02lx)\n",
		        context, (ULONG)istat);
		// Clear SCSI status
		(void)ncr->sstat0;
		(void)ncr->sstat1;
	}

	return 0;
}
