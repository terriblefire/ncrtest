# Scatter-Gather DMA Test Implementation

## Overview

Added scatter-gather DMA testing capability to `ncr_dmatest`. This demonstrates the NCR 53C710's ability to chain multiple Memory Move instructions in a single SCRIPTS program, executing them sequentially without CPU intervention.

## What is Scatter-Gather?

Scatter-gather is a DMA technique where data is transferred between multiple non-contiguous memory regions in a single operation:

- **Gather**: Read from multiple scattered source buffers → write to one contiguous destination
- **Scatter**: Read from one contiguous source → write to multiple scattered destinations

This mirrors what real SCSI I/O does with virtual memory pages.

## Implementation Details

### New Constants (ncr_dmatest.h)

```c
#define MAX_SG_SEGMENTS   8           // Maximum scatter-gather segments
#define SG_SEGMENT_SIZE   (4*1024)    // Size of each segment (4KB)
```

### New Functions (ncr_dmatest.c)

#### 1. `BuildScatterGatherScript()`

Builds a SCRIPTS program with multiple chained Memory Move instructions:

```c
static ULONG* BuildScatterGatherScript(UBYTE **sources, UBYTE *dest,
                                        ULONG *sizes, ULONG num_segments)
```

**Structure:**
```
[Memory Move 1: source[0] → dest+0      ]  // 8 bytes
[Memory Move 2: source[1] → dest+offset1]  // 8 bytes
[Memory Move 3: source[2] → dest+offset2]  // 8 bytes
...
[Memory Move N: source[N-1] → dest+offsetN-1]
[Interrupt: 0xCAFEBABE                 ]  // 8 bytes (completion signal)
```

**Key Features:**
- Each Memory Move is 8 bytes (opcode 0xC0)
- Destination addresses are calculated to be contiguous
- Uses magic value 0xCAFEBABE (different from regular DMA's 0xDEADBEEF)
- All instructions fetched and executed by NCR chip automatically

#### 2. `RunScatterGatherTest()`

Executes the scatter-gather SCRIPTS program:

```c
static LONG RunScatterGatherTest(volatile struct ncr710 *ncr, UBYTE **sources,
                                  UBYTE *dest, ULONG *sizes, ULONG num_segments)
```

**Execution Flow:**
1. Build SCRIPTS program
2. Flush caches for coherency
3. Load DSP register with script address
4. Poll for DIP interrupt
5. Check for 0xCAFEBABE completion signal
6. Return status

#### 3. `TestScatterGather()`

High-level test function that:

- Allocates gather destination buffer
- Selects source segments from different memory regions:
  - CHIP RAM (buffer 1) with PATTERN_WALKING
  - MB_FAST with PATTERN_ALTERNATING
  - CPU_FASTL with PATTERN_ONES
  - CHIP RAM (buffer 2) with PATTERN_ZEROS
- Builds and executes scatter-gather SCRIPTS
- Verifies each segment was copied correctly
- Reports results

## What Gets Tested

### Memory Region Switching

The test gathers from up to 4 different memory regions:

1. **CHIP RAM** (0x00000000-0x001FFFFF) - Custom chip bus
2. **MB_FAST** (0x07000000-0x07FFFFFF) - 16-bit motherboard RAM
3. **CPU_FASTL** (0x08000000-0x0FFFFFFF) - 32-bit burst RAM
4. **CHIP RAM again** - Tests returning to same controller

This tests if the DMA engine can:
- Switch between different memory controllers mid-flight
- Handle different bus widths (16-bit vs 32-bit)
- Maintain burst mode settings across regions
- Handle cache coherency across regions

### Zero CPU Intervention

The key innovation: **All 4 DMA operations complete without a single CPU interrupt**

Traditional approach:
```
DMA segment 1 → CPU INTERRUPT (80μs) → DMA segment 2 →
CPU INTERRUPT (80μs) → DMA segment 3 → CPU INTERRUPT (80μs) →
DMA segment 4 → CPU INTERRUPT (80μs)
Total overhead: 320μs of CPU time
```

NCR 53C710 SCRIPTS approach:
```
Load DSP register once → Hardware executes all 4 moves →
Single interrupt when complete
Overhead: 2-8 bus cycles per instruction fetch (~1μs total)
```

**Savings: 99.7% reduction in overhead!**

## Example Output

```
=== Scatter-Gather DMA Tests ===
Gather destination: 0x08100000

*** Test 1: Gather from multiple memory regions ***
  Segment 0: CHIP RAM     0x00012340 -> 0x08100000 (4096 bytes)
  Segment 1: MB_FAST      0x07050000 -> 0x08101000 (4096 bytes)
  Segment 2: CPU_FASTL    0x08200000 -> 0x08102000 (4096 bytes)
  Segment 3: CHIP RAM     0x00016340 -> 0x08103000 (4096 bytes)

Executing scatter-gather SCRIPTS with 4 segments (16384 bytes total)...
This will execute 4 Memory Move instructions sequentially
without any CPU intervention!

Verifying gathered data...
  Segment 0: VERIFIED (4096 bytes)
  Segment 1: VERIFIED (4096 bytes)
  Segment 2: VERIFIED (4096 bytes)
  Segment 3: VERIFIED (4096 bytes)

*** Scatter-Gather Test: PASSED ***
Successfully gathered 4 segments (16384 bytes) from different
memory regions using a single SCRIPTS program with zero CPU
intervention between segments!
```

## Why This Matters for A4000T

The A4000T has known DMA issues with certain memory configurations. Scatter-gather testing reveals:

1. **Cross-region coherency**: Does the NCR maintain cache coherency when switching between memory controllers?

2. **Burst mode stability**: Does burst mode work correctly when crossing memory boundaries?

3. **Bus arbitration**: Can the DMA engine properly arbitrate between different buses (Zorro III, CPU local, custom chip)?

4. **Real-world workload**: This mirrors actual SCSI I/O with virtual memory, making it a realistic stress test.

## Technical Notes

### SCRIPTS Buffer Size

The existing 256-byte buffer is sufficient:
- 8 segments × 8 bytes = 64 bytes for Memory Moves
- 1 interrupt × 8 bytes = 8 bytes
- Total: 72 bytes (well within 256-byte limit)

### Magic Values

- Regular DMA: `0xDEADBEEF` in DSPS
- Scatter-gather: `0xCAFEBABE` in DSPS

Different values allow distinguishing which type of test completed.

### Memory Alignment

Source buffers come from `AllocMem()` which returns longword-aligned addresses. However, the gather destination is also longword-aligned, ensuring optimal burst performance.

### Cache Coherency

`CacheClearU()` is called before and after DMA to ensure:
- Source data is flushed to RAM before DMA reads it
- Destination data is invalidated so CPU reads fresh data

## Future Enhancements

Possible additions:

1. **Scatter Test**: One source → multiple destinations (inverse of gather)
2. **Variable Segment Sizes**: Test non-uniform segment sizes
3. **Unaligned Segments**: Test unaligned addresses to stress memory controllers
4. **Performance Measurement**: Time scatter-gather vs sequential single-segment transfers
5. **Table Indirect Mode**: Use DSA register and memory table instead of hardcoded addresses

## References

- NCR 53C710 Programmer's Guide, Chapter 7: "SCSI SCRIPTS to Support Use of Scatter/Gather"
- Current implementation: `ncr_dmatest.c` lines 224-683
