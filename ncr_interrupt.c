/*
 * NCR 53C710 Interrupt Handler
 */

#include "ncr_dmatest.h"
#include "dprintf.h"
#include <stdio.h>
#include <exec/execbase.h>
#include <exec/interrupts.h>
#include <proto/exec.h>
#include <hardware/intbits.h>

#define printf dbgprintf

/* Global interrupt statistics */
static volatile ULONG g_int_count = 0;
static volatile ULONG g_dma_int_count = 0;
static volatile ULONG g_scsi_int_count = 0;
static volatile UBYTE g_last_istat = 0;
static volatile UBYTE g_last_dstat = 0;
static volatile UBYTE g_last_sstat0 = 0;
static volatile ULONG g_last_dsp = 0;

/* NCR chip pointer for interrupt handler */
static volatile struct ncr710 *g_ncr = NULL;

/* Interrupt server structure */
static struct Interrupt ncr_interrupt = {
	{NULL, NULL, NT_INTERRUPT, 0, "NCR53C710"},
	NULL,  /* IS_DATA - will be set to NCR base */
	NULL   /* IS_CODE - will be set to our handler */
};

/*
 * NCR 53C710 Interrupt Handler
 * Called by Exec when NCR generates an interrupt
 *
 * Parameters come in registers:
 * a1 = custom data pointer (NCR base address)
 * a6 = SysBase
 */
__attribute__((saveds)) ULONG NCRIntHandler(
	register volatile struct ncr710 *ncr asm("a1"),
	register struct ExecBase *SysBase asm("a6"))
{
	UBYTE istat, dstat, sstat0;
	ULONG dsp;

	/* Read interrupt status */
	istat = ncr->istat;

	/* Check if this is really our interrupt */
	if (!(istat & (ISTATF_DIP | ISTATF_SIP))) {
		return 0;  /* Not our interrupt */
	}

	g_int_count++;
	g_last_istat = istat;

	/* Read additional status */
	dstat = ncr->dstat;
	sstat0 = ncr->sstat0;
	dsp = ncr->dsp;

	g_last_dstat = dstat;
	g_last_sstat0 = sstat0;
	g_last_dsp = dsp;

	/* DMA interrupt? */
	if (istat & ISTATF_DIP) {
		g_dma_int_count++;

		/* Log DMA interrupts */
		printf("[INT] DMA interrupt: DSTAT=0x%02x DSP=0x%08lx\n",
		       dstat, dsp);

		if (dstat & DSTATF_IID) {
			printf("[INT]   Illegal Instruction Detected\n");
		}
		if (dstat & DSTATF_SIR) {
			ULONG dsps = ncr->dsps;
			printf("[INT]   Script Interrupt: DSPS=0x%08lx\n", dsps);
		}
		if (dstat & DSTATF_SSI) {
			printf("[INT]   Single Step Interrupt\n");
		}
		if (dstat & DSTATF_ABRT) {
			printf("[INT]   Aborted\n");
		}
		if (dstat & DSTATF_BF) {
			printf("[INT]   Bus Fault\n");
		}
		if (dstat & DSTATF_DFE) {
			printf("[INT]   DMA FIFO Empty\n");
		}
	}

	/* SCSI interrupt? */
	if (istat & ISTATF_SIP) {
		g_scsi_int_count++;

		printf("[INT] SCSI interrupt: SSTAT0=0x%02x\n", sstat0);

		if (sstat0 & 0x80) {
			printf("[INT]   Phase Mismatch\n");
		}
		if (sstat0 & 0x40) {
			printf("[INT]   Function Complete\n");
		}
		if (sstat0 & 0x20) {
			printf("[INT]   Selection Timeout\n");
		}
		if (sstat0 & 0x10) {
			printf("[INT]   Selected\n");
		}
		if (sstat0 & 0x08) {
			printf("[INT]   SCSI Gross Error\n");
		}
		if (sstat0 & 0x04) {
			printf("[INT]   Unexpected Disconnect\n");
		}
		if (sstat0 & 0x02) {
			printf("[INT]   SCSI RST Received\n");
		}
		if (sstat0 & 0x01) {
			printf("[INT]   Parity Error\n");
		}
	}

	/* Clear interrupt by reading status registers (already done above) */
	return 1;  /* We handled this interrupt */
}

/*
 * Install NCR interrupt handler
 */
LONG InstallNCRInterrupt(volatile struct ncr710 *ncr)
{
	printf("Installing NCR interrupt handler...\n");

	g_ncr = ncr;
	g_int_count = 0;
	g_dma_int_count = 0;
	g_scsi_int_count = 0;

	/* Set up interrupt structure */
	ncr_interrupt.is_Data = (APTR)ncr;
	ncr_interrupt.is_Code = (void(*)())NCRIntHandler;

	/* Add our interrupt handler to the PORTS chain (IRQ 2) */
	AddIntServer(INTB_PORTS, &ncr_interrupt);

	printf("  Interrupt handler installed on PORTS chain\n");

	/* Enable NCR interrupts */
	printf("  Enabling NCR interrupts...\n");
	ncr->sien = 0xFF;  /* Enable all SCSI interrupts */
	ncr->dien = 0xFF;  /* Enable all DMA interrupts */

	printf("  NCR interrupts enabled: SIEN=0x%02lx DIEN=0x%02lx\n",
	       (ULONG)ncr->sien, (ULONG)ncr->dien);

	return 0;
}

/*
 * Remove NCR interrupt handler
 */
void RemoveNCRInterrupt(void)
{
	if (g_ncr) {
		printf("Removing NCR interrupt handler...\n");

		/* Disable NCR interrupts */
		g_ncr->sien = 0;
		g_ncr->dien = 0;

		/* Remove interrupt handler */
		RemIntServer(INTB_PORTS, &ncr_interrupt);

		printf("  Interrupt handler removed\n");
		printf("  Total interrupts: %ld (DMA: %ld, SCSI: %ld)\n",
		       g_int_count, g_dma_int_count, g_scsi_int_count);

		g_ncr = NULL;
	}
}

/*
 * Get interrupt statistics
 */
void GetNCRInterruptStats(ULONG *total, ULONG *dma, ULONG *scsi)
{
	if (total) *total = g_int_count;
	if (dma) *dma = g_dma_int_count;
	if (scsi) *scsi = g_scsi_int_count;
}

/*
 * Get last interrupt details
 */
void GetLastNCRInterrupt(UBYTE *istat, UBYTE *dstat, UBYTE *sstat0, ULONG *dsp)
{
	if (istat) *istat = g_last_istat;
	if (dstat) *dstat = g_last_dstat;
	if (sstat0) *sstat0 = g_last_sstat0;
	if (dsp) *dsp = g_last_dsp;
}
