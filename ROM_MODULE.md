# ROM Module - Making ncr_dmatest a Resident Command

The `ncr_dmatest.resource` file is a ROM-resident module that makes the `ncr_dmatest` command available at boot time, following the same pattern as the cpufreq tool.

## What It Does

When added to your kickstart ROM, the module:
1. Registers `ncr_dmatest` as an internal command using `AddSegment()`
2. Embeds the gcc-compiled executable inside the ROM module
3. Loads and executes the embedded executable when the command is run
4. Makes the command available from any directory without needing the file on disk

## Building the ROM Module

The ROM module requires both gcc (for the main executable) and vbcc (for the ROM wrapper):

```bash
export VBCC=/opt/vbcc
make
```

This builds:
- `ncr_dmatest` (13KB) - Standard executable (gcc)
- `ncr_dmatest.resource` (14KB) - ROM module (vbcc wrapper + embedded gcc executable)

## Architecture

Following the cpufreq pattern:

### rom_resident.c
- ROM tag structure with `RTC_MATCHWORD`, `RTF_AFTERDOS` flag
- Version info: "ncr_dmatest 0.01 (28.10.2025)"
- Points to `Init()` function

### rom_main.c
- `Init()` - Called by Exec, registers command with `AddSegment()`
- `Run()` - Called when command is executed, loads embedded executable
- `Copy()`, `Alloc()`, `Free()` - Helper functions for `InternalLoadSeg()`
- Uses vbcc `__reg()` calling convention for Amiga OS compatibility

### rom_payload.s
- `@Launch` - Assembly trampoline to call the loaded executable
- `_ncr_dmatest_cli` - Embeds the gcc-built executable binary via `incbin`
- `_end` - Marks the end of the embedded data

## Adding to Kickstart ROM

### Option 1: Using romtool (Recommended)

1. Split your kickstart ROM:
```bash
python3 -m venv venv
source venv/bin/activate
pip install amitools
romtool split -o split_rom kickstart.rom
```

2. Add `ncr_dmatest.resource` to the ROM:
```bash
cp ncr_dmatest.resource split_rom/
echo "ncr_dmatest.resource" >> split_rom/index.txt
```

3. Rebuild the ROM:
```bash
romtool build -o kickstart_patched.rom split_rom/index.txt
```

4. Verify the module is present:
```bash
romtool scan kickstart_patched.rom | grep ncr_dmatest
```

### Option 2: Replace Existing Module

If you want to replace an existing module (like NCRscsi.device):

```bash
# Remove the old module from index
grep -v "NCRscsi.device" split_rom/index.txt > split_rom/index_new.txt
# Add our module
echo "ncr_dmatest.resource" >> split_rom/index_new.txt
# Rebuild
romtool build -o kickstart_patched.rom split_rom/index_new.txt
```

## Testing

After flashing the patched ROM:

1. Boot your Amiga 4000T
2. Open a Shell
3. Type `ncr_dmatest` from any directory
4. The command should run without needing the file on disk

## Memory Usage

The resident command uses approximately 14KB of ROM space and loads the embedded executable (10.6KB) into RAM when executed.

## Technical Details

### Why vbcc for the Wrapper?

The ROM wrapper uses vbcc because:
- It supports `__reg()` attribute for Amiga register calling conventions
- Required for proper integration with Exec's `Init()` callback
- Required for DOS `InternalLoadSeg()` callbacks (`Copy`, `Alloc`, `Free`)
- gcc doesn't support these calling conventions directly

### Why gcc for the Main Code?

The main DMA test code uses gcc because:
- Better optimization for 68040
- More modern C features
- Easier debugging
- Already proven to work with Amiberry emulation

### Hybrid Approach

This project uses a **hybrid approach**:
1. Main code (main.c, ncr_init.c, ncr_dmatest.c) compiled with **gcc**
2. ROM wrapper (rom_resident.c, rom_main.c) compiled with **vbcc**
3. Assembly payload (rom_payload.s) embeds the gcc executable
4. Final ROM module links everything together with vlink

This gives us the best of both worlds: modern gcc for the main code, and vbcc compatibility for ROM integration.

## Comparison with Standard Executable

| Feature | Standard Executable | ROM Module |
|---------|-------------------|------------|
| File size on disk | 13KB | 0 bytes (in ROM) |
| Available at boot | No (needs C: directory) | Yes (always available) |
| Memory usage | 10.6KB when run | 14KB ROM + 10.6KB when run |
| Compiler | gcc | vbcc wrapper + gcc code |
| Installation | Copy to C: | Flash to ROM |

## Building for Docker CI

The GitHub Actions workflow uses the gcc Docker container to build the standard executable, but the ROM module requires vbcc which isn't available in Docker. The ROM module build is intended for local development only.

## Future Improvements

- Could create a separate workflow for ROM module builds if vbcc Docker image becomes available
- Could automate the kickstart patching process
- Could add options to run tests automatically at boot (via Init function)

## References

- cpufreq example: `/Users/stephen/git/amiga_code/cpufreq`
- amitools romtool: https://github.com/cnvogelg/amitools
- vbcc compiler: http://www.compilers.de/vbcc.html
