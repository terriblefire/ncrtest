# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

NCR 53C710 DMA test tool for Amiga 4000T. Tests DMA paths between memory areas using the NCR SCSI chip's internal DMA engine without accessing physical disks. Produces two outputs:
- **ncr_dmatest** - Standard Amiga executable (~10KB)
- **ncr_dmatest.resource** - ROM-resident module (~14KB, optional)

## Build Commands

**Standard build (executable only, gcc):**
```bash
make ncr_dmatest
```

**Build all targets (requires vbcc at /opt/vbcc):**
```bash
export VBCC=/opt/vbcc
make all
```
The default `make` target attempts to build both the executable and ROM module. If vbcc is not available, it will fail on the ROM module step.

**Docker build (recommended for CI/CD):**
```bash
docker run --rm \
  -v ${PWD}:/work \
  -w /work \
  amigadev/crosstools:m68k-amigaos \
  bash -c "make clean && make ncr_dmatest"
```

**Build patched kickstart ROM:**
```bash
make kickstart
```
This requires:
- ROM module built (`ncr_dmatest.resource`)
- Python 3 virtual environment (created automatically)
- `amitools` package (installed automatically via pip)
- Original kickstart ROM at `roms/kick4000.rom`
- Removes workbench.library to make space for the test module
- Outputs patched ROM to `roms/kickstart_patched.rom`

**Clean and rebuild:**
```bash
make clean        # Remove build artifacts
make distclean    # Deep clean (includes ROM files and venv)
make rebuild      # Clean + build
```

**Debug makefile:**
```bash
make show         # Display makefile variables
```

## Build Requirements

- **m68k-amigaos-gcc** - 68040 cross-compiler (required)
- **Amiga NDK includes** (required)
- **vbcc** at /opt/vbcc (optional, for ROM module only)
- **Python 3 + amitools** (optional, for kickstart patching only)

**Important:** Use Docker for consistent builds. GitHub Actions CI/CD uses `amigadev/crosstools:m68k-amigaos` container.

## Compiler Flags

**GCC (standard executable):**
- `-Os` - Optimize for size
- `-m68040` - Target 68040 CPU
- `-noixemul` - Don't use ixemul.library
- `-fomit-frame-pointer` - Omit frame pointer for smaller code
- `-fmerge-all-constants` - Merge identical constants

**VBCC (ROM module):**
- `-O2 -size` - Optimize for size
- `-cpu=68040` - Target 68040 CPU
- `-fastcall` - Use fast calling convention
- `-nostdlib` - No standard library
- `-c99` - C99 mode
- `+aos68k` - AmigaOS target

## Architecture

### Two Build Targets

1. **Standard Executable** (`ncr_dmatest`)
   - Compiled with gcc (-Os -m68040 -noixemul)
   - Sources: main.c, ncr_init.c, ncr_dmatest.c, dprintf.c
   - Output: Amiga hunk executable (~10KB)
   - Target: `make ncr_dmatest`

2. **ROM Module** (`ncr_dmatest.resource`)
   - Hybrid gcc + vbcc approach
   - ROM wrapper (vbcc): rom_resident.c, rom_main.c
   - Assembly payload: rom_payload.s (embeds gcc executable via `incbin`)
   - Makes command available at boot without disk file
   - Target: `make ncr_dmatest.resource` (requires `ncr_dmatest` built first)
   - **Dependency:** ROM module build embeds the gcc-built executable, so standard executable must be built first

### Build Target Dependencies

```
make all
├── ncr_dmatest (gcc)
│   ├── main.o
│   ├── ncr_init.o
│   ├── ncr_dmatest.o
│   └── dprintf.o
└── ncr_dmatest.resource (vbcc + asm)
    ├── rom_resident.o (vbcc)
    ├── rom_main.o (vbcc)
    └── rom_payload.o (asm, incbin ncr_dmatest)
```

### Core Components

**main.c** - Standard Amiga executable entry point
- `main()` - Prints version, calls TestMain()

**ncr_init.c** - NCR 53C710 chip initialization
- `DetectNCR()` - Configure GARY chipset, check for NCR presence
- `ResetNCR()` - Software reset, disable burst mode, configure ctest0
- `InitNCR()` - Setup DMA mode, disable SCSI operations, use polling
- `CheckNCRStatus()` - Check for DMA errors (IID, ABRT, WTD)

**ncr_dmatest.c** - DMA testing implementation
- `BuildDMAScript()` - Creates 2-instruction SCRIPTS program (move + interrupt)
- `RunDMATest()` - Execute script by writing to DSP, poll ISTAT/DSTAT
- `FillPattern()` - Fill buffer with test patterns (zeros, ones, walking, alternating, random)
- `VerifyBuffer()` - Compare source and destination byte-by-byte
- `TestMemoryTypes()` - Allocate buffers, run comprehensive tests
- `TestMain()` - Entry point for all tests

**ncr_dmatest.h** - Header file
- `struct ncr710` - Big-endian register structure
- `struct memmove_inst` - SCRIPTS memory-move instruction format
- NCR register bit definitions (DMODE, DCNTL, ISTAT, DSTAT, etc.)
- Test parameters (buffer sizes, patterns, status codes)

**dprintf.c** - Dual output debugging
- `dbgprintf()` - Outputs to both printf() and RawPutChar() for visibility

### Critical Hardware Details

**NCR 53C710 Address:** 0x00DD0040
**Write offset for longwords:** +0x80 bytes

**GARY Chipset Configuration:**
- Must configure GARY (0x00DE0000) before accessing NCR
- Set DSACK timeout mode, then check timeout bit
- Set DCNTL.EA bit before any other register access (critical for A4000T)

**Register Access Pattern:**
```c
#define WRITE_LONG(base,reg,val) \
    *((volatile ULONG *) (((ULONG) (base)) + NCR_WRITE_OFFSET + \
                          offsetof(struct ncr710,reg))) = (val)
```

**DMA Configuration:**
- Burst length: 8 transfers (DMODE = BL1|BL0|FC2)
- Clock: 50MHz, no divider
- Mode: Polling (interrupts disabled to avoid need for handler)
- SCSI operations: Disabled (no bus activity)

### SCRIPTS Programming

NCR uses a SCRIPTS language for DMA. Each test builds a simple 2-instruction program:

```c
struct memmove_inst {
    UBYTE op;      // 0xC0 = memory move
    UBYTE len[3];  // 24-bit byte count
    ULONG source;  // source address
    ULONG dest;    // destination address
};
```

Followed by interrupt instruction (0x98) with magic value (0xDEADBEEF) in DSPS.

**Execution flow:**
1. Write script address to DSP register
2. Poll ISTAT for DIP (DMA interrupt pending)
3. Check DSTAT for SIR (script interrupt) or errors
4. Verify DSPS == 0xDEADBEEF for completion

### Memory Region Testing

Tests all permutations between memory regions:
- **Chip RAM** (MEMF_CHIP)
- **MB_FAST** (0x07000000-0x07FFFFFF, allocated via AllocAbs in 64KB increments)
- **CPU_FASTL** (0x08000000-0x0FFFFFFF, allocated via AllocAbs)
- **CPU_FASTU** (0x10000000-0x18000000, allocated via AllocAbs)

Each region gets two buffers for bidirectional testing. SCRIPTS buffer allocated in MEMF_FAST.

### Test Patterns

- **PATTERN_ZEROS** (0x00)
- **PATTERN_ONES** (0xFF)
- **PATTERN_WALKING** (1 << (i & 7))
- **PATTERN_ALTERNATING** (0x55/0xAA)
- **PATTERN_RANDOM** (pseudo-random via linear congruential generator)

Test sizes: MIN_TEST_SIZE (4 bytes) to MAX_TEST_SIZE (16KB) in powers of 2.

## ROM Module Architecture

**Why hybrid gcc + vbcc?**
- gcc: Better optimization, modern C, proven with Amiberry
- vbcc: Required for `__reg()` calling convention for Exec and DOS callbacks

**Structure:**
- `rom_resident.c` - ROM tag (RTC_MATCHWORD, RTF_AFTERDOS)
- `rom_main.c` - Init() registers command with AddSegment(), Run() loads embedded executable
- `rom_payload.s` - Assembly trampoline + incbin of gcc-built executable

## Common Issues

- **Bus hang**: DCNTL.EA must be set before any NCR register access
- **Timeout errors**: Check ISTAT/DSTAT, verify SCRIPTS program correctness
- **Verify errors**: Data mismatch indicates DMA or memory problem
- **Illegal instruction (IID)**: SCRIPTS program has invalid opcode/format
- **Memory allocation fails**: Not enough Chip/Fast RAM available
- **Burst mode issues**: ctest7.CDIS disables burst mode (critical after reset)

## Code Conventions

- Use volatile pointers for all NCR register access
- Use ULONG/UBYTE types from exec/types.h
- Call CacheClearU() before/after DMA operations
- All memory allocated via AllocMem/AllocAbs, freed on exit (atexit cleanup)
- Use printf for user output, dbgprintf for debugging (dual output)
- Requires supervisor access for hardware register access

## CI/CD Pipeline

GitHub Actions workflow (`.github/workflows/build.yml`) automatically:
- Builds on push/PR to main, master, or develop branches
- Uses Docker container: `amigadev/crosstools:m68k-amigaos`
- Builds only the standard executable (`ncr_dmatest`) - ROM module excluded from CI
- Uploads artifacts with 90-day retention
- Creates GitHub releases on tags with executable attachment

**Note:** ROM module is not built in CI/CD because it requires vbcc which is not available in the Docker container.

## Reference Code

Based on NCR driver from /Users/stephen/git/kickstart/scsidisk:
- ncr710.h - Register definitions
- ncr.c - NCR driver implementation
- script.c - SCRIPTS program
- init.asm - ROM module structure

ROM module pattern from /Users/stephen/git/amiga_code/cpufreq