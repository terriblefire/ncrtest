/*
 * NCR 53C710 DMA Test Tool - Main Entry Point
 *
 * Standard Amiga executable version
 */

#include "ncr_dmatest.h"
#include <stdio.h>
#include <proto/exec.h>

int main(int argc, char **argv)
{
	printf("\n%s\n", VERSION_STRING);
	printf("==============================\n\n");
	printf("This tool tests the NCR 53C710 DMA engine.\n");
	printf("WARNING: This requires supervisor access!\n\n");

	/* Call the test main function */
	TestMain();

	return 0;
}
