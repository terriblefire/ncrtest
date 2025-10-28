# NCR 53C710 DMA Test Tool for Amiga 4000T

[![Build NCR DMA Test Tool](https://github.com/terriblefire/ncrtest/actions/workflows/build.yml/badge.svg)](https://github.com/terriblefire/ncrtest/actions/workflows/build.yml)

**Version:** ncrtest 0.01 (28.10.2025)

A standard Amiga executable that tests DMA paths between different memory areas using the NCR 53C710 SCSI chip on the Amiga 4000T. This tool does **not** access the SCSI bus or any physical disks - it only uses the chip's internal DMA engine for memory-to-memory transfers.

## Features

- Tests DMA transfers between different memory types:
  - Chip RAM to Chip RAM
  - Fast RAM to Fast RAM
  - Chip RAM to Fast RAM
  - Fast RAM to Chip RAM

- Multiple test patterns:
  - All zeros (0x00)
  - All ones (0xFF)
  - Walking ones pattern
  - Alternating 0x55/0xAA pattern
  - Pseudo-random data

- Various transfer sizes from 256 bytes to 16KB

- Console output via printf

- Automatic verification of transferred data

## Building

Requirements:
- m68k-amigaos-gcc cross-compiler (68040 target)
- Amiga NDK includes

Build the executable:
```bash
make
```

This produces `ncr_dmatest` - a standard Amiga executable.

Build patched kickstart ROM (optional):
```bash
make kickstart
```

This creates `roms/kickstart_patched.rom` with the test tool integrated as a ROM module.

Clean build artifacts:
```bash
make clean
```

Deep clean (including ROM splits and venv):
```bash
make distclean
```

## Architecture

The tool consists of several components:

### main.c
Standard Amiga executable entry point:
- `main()` - Entry point, displays version and calls TestMain()

### ncr_dmatest.h
Header file with NCR 53C710 register definitions, instruction formats, version string, and function prototypes.

### ncr_init.c
Initialization and reset code for the NCR chip:
- `InitNCR()` - Initialize chip for DMA testing (does NOT enable SCSI operations)
- `ResetNCR()` - Software reset of the chip
- `CheckNCRStatus()` - Check for errors and interrupts

### ncr_dmatest.c
Main DMA test implementation:
- `BuildDMAScript()` - Creates SCRIPTS program for memory-to-memory DMA
- `RunDMATest()` - Executes a single DMA transfer
- `FillPattern()` - Fills buffer with test patterns
- `VerifyBuffer()` - Verifies transferred data matches source
- `RunComprehensiveTest()` - Runs full test suite
- `TestMemoryTypes()` - Tests all memory type combinations
- `TestMain()` - Main test entry point

## How It Works

1. User runs the `ncr_dmatest` executable from Workbench or CLI
2. The program calls `TestMain()` to begin testing
3. NCR chip is reset and configured for DMA-only operation
4. Memory buffers are allocated in both Chip and Fast RAM
5. For each test:
   - Source buffer is filled with a test pattern
   - Destination buffer is cleared
   - A SCRIPTS program is built to perform the DMA transfer
   - The script is executed by loading it into the DSP register
   - Transfer completion is detected via interrupt
   - Destination buffer is verified against source
6. Results are output to the console using printf

## SCRIPTS Programming

The NCR 53C710 uses a scripting language called SCRIPTS for DMA operations. This tool uses memory-to-memory move instructions:

```c
struct memmove_inst {
    UBYTE op;      // 0xC0 = memory move
    UBYTE len[3];  // 24-bit byte count
    ULONG source;  // source address
    ULONG dest;    // destination address
};
```

Each test builds a simple 2-instruction script:
1. Memory move instruction (copies data)
2. Interrupt instruction (signals completion)

## Register Configuration

Key NCR registers used:
- **DMODE** - DMA mode (burst length, function codes)
- **DCNTL** - DMA control (clock divider, enable ack)
- **DIEN** - DMA interrupt enable
- **DSP** - SCRIPTS pointer (write to start execution)
- **DSTAT** - DMA status (read to check for errors/completion)
- **ISTAT** - Interrupt status

The chip is configured to:
- Use 8-transfer bursts
- Supervisor data access
- 50MHz clock (no divider)
- Interrupts for illegal instructions, aborts, and completion

## Memory Addresses

- NCR 53C710 chip: 0x00DD0040
- Write offset for longwords: +0x80 bytes

## Output

All output goes to the console via standard printf. Run the tool from CLI or Shell to see the test results, or redirect output to a file.

## License

This is a diagnostic tool for Amiga 4000T hardware testing.
