# Build Notes

## Build Success

The NCR 53C710 DMA test tool has been successfully built as a standard Amiga executable!

**Version:** ncrtest 0.01 (28.10.2025)

```
Executable built: ncr_dmatest
   text	   data	    bss	    dec	    hex	filename
   9648	    288	    144	  10080	   2760	ncr_dmatest
```

**Output file**: `ncr_dmatest` (10KB)
**Format**: Standard Amiga executable (hunk format)

## Build Details

- **Text section**: 9648 bytes (code)
- **Data section**: 288 bytes (initialized data)
- **BSS section**: 144 bytes (uninitialized data)
- **Total size**: 10,080 bytes (~10KB)

## Key Build Configuration

1. **Compiler**: m68k-amigaos-gcc with `-O2 -m68040 -noixemul -fomit-frame-pointer`
2. **Linker**: `-noixemul` for standard executable
3. **Output format**: printf to console (not kprintf to serial port)
4. **Source files**: main.c, ncr_init.c, ncr_dmatest.c (no assembly)

## Building

```bash
make              # Build the standard executable
make kickstart    # Build patched kickstart ROM (optional)
make clean        # Remove build artifacts
make distclean    # Deep clean (including ROM files and venv)
make rebuild      # Clean + build
```

## Using the Executable

To use this tool on your Amiga 4000T:

1. Copy `ncr_dmatest` to your Amiga
2. Run from CLI or Shell
3. View test output on console
4. Requires supervisor access to access NCR chip hardware

## Files Generated

- `ncr_dmatest` - The executable (main output)
- `main.o` - Entry point object
- `ncr_init.o` - NCR initialization object
- `ncr_dmatest.o` - DMA test routines object
- `roms/kickstart_patched.rom` - Optional patched kickstart ROM (via `make kickstart`)

## Testing

This tool will:
- Initialize the NCR 53C710 chip for DMA-only operation
- Allocate test buffers in Chip and Fast RAM
- Run comprehensive DMA tests with multiple patterns and sizes
- Test all memory type combinations (Chip↔Chip, Fast↔Fast, Chip↔Fast, Fast↔Chip)
- Verify all transferred data
- Report results to console via printf

All output goes to the console.
