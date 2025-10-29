# Building Patched Kickstart ROM

This document describes how to build a patched kickstart ROM with the `ncr_dmatest.resource` module integrated.

## Quick Start

```bash
export VBCC=/opt/vbcc
make kickstart
```

This will:
1. Create a Python venv and install amitools
2. Split your kick4000.rom file
3. Remove non-essential modules to make space
4. Add ncr_dmatest.resource to the ROM
5. Rebuild the kickstart ROM as `roms/kickstart_patched.rom`

## Requirements

- Original kickstart ROM: `roms/kick4000.rom` (512KB)
- Python 3.x
- vbcc installed at `/opt/vbcc`
- Enough disk space for ROM splitting (~1MB)

## What Gets Removed

To make space for the ncr_dmatest.resource module (14KB), these non-essential modules are removed:

- `potgo.resource` - Pot/Gameport control (for joysticks/mice)
- `audio.device` - Audio device driver
- `wbtask` - Workbench task
- `ramdrive` - RAM disk driver
- `shell_40.2` - AmigaShell (17KB)
- `bootmenu_40.5` - Boot menu
- `romboot_40.1` - ROM boot code

**Total space freed:** ~30KB
**ncr_dmatest.resource size:** 14KB

## What Remains

All essential system components remain:
- ✅ exec, dos.library, intuition.library
- ✅ graphics.library, gadtools.library
- ✅ console.device, filesystem
- ✅ scsi.device, trackdisk.device
- ✅ All critical system resources

## Output

**File:** `roms/kickstart_patched.rom` (512KB)

```
@00076938  +0007a050  NT_UNKNOWN  +0  ncr_dmatest  ncr_dmatest 0.01 (29.10.2025)
```

## Verification

Verify the patched ROM contains the module:

```bash
make verify-kickstart
```

This will scan the ROM and show all modules, including ncr_dmatest.

## Testing in Emulator

### Amiberry

1. Copy `roms/kickstart_patched.rom` to Amiberry ROM directory
2. Configure A4000 with the patched ROM
3. Boot and open Shell
4. Type `ncr_dmatest` - command should be available!

### FS-UAE

1. Point kickstart_file to `roms/kickstart_patched.rom`
2. Boot system
3. The ncr_dmatest command will be resident

## Installing on Real Hardware

⚠️ **IMPORTANT:** Make backups of your original ROMs before proceeding!

### Option 1: EEPROM/EPROM

1. Burn `roms/kickstart_patched.rom` to a 512KB EPROM (27C040 or compatible)
2. Install in your A4000T kickstart socket
3. Boot system

### Option 2: ROM Emulator

1. Load `roms/kickstart_patched.rom` into ROM emulator
2. Boot system

### Option 3: Soft-Kick (Software)

Some tools allow loading kickstart from disk:
- Use a soft-kick loader
- Load `roms/kickstart_patched.rom`
- This is temporary until reboot

## What You Lose

### Removed Functionality

- **No Shell in ROM** - You'll need to load shell from disk or use Workbench CLI
- **No audio.device** - Audio functionality may be limited (though most software loads this from disk anyway)
- **No boot menu** - Can't access Early Startup menu
- **No RAM drive** - No automatic RAM: disk creation
- **No potgo** - Gameport/joystick functionality may be affected

### Workarounds

Most removed modules can be loaded from disk:
- Shell can be run from C:Shell on disk
- Audio drivers typically load from DEVS:
- RAM disk can be created with Mount command

## Restoring Original ROM

To go back to the original:
1. Re-flash original `kick4000.rom`
2. Or use ROM socket switch
3. Or just keep both ROMs and swap as needed

## Cleaning Up

Remove build artifacts and venv:

```bash
make distclean
```

This removes:
- `roms/split/` directory
- `roms/kickstart_patched.rom`
- `venv/` directory

## Troubleshooting

### "Module does not fit into ROM"

Try removing more modules by editing the Makefile's `kickstart` target. Safe candidates:
- mathffp.library
- mathieeesingbas.lib
- icon.library
- keymap.library

### "kick4000.rom not found"

Place your original kickstart ROM at `roms/kick4000.rom`

### "romtool not found"

The venv didn't install properly. Run:
```bash
rm -rf venv
make kickstart
```

## ROM Structure

The patched ROM is organized as:
1. Essential system modules (exec, dos, etc.)
2. Device drivers (scsi, console, etc.)
3. System libraries (intuition, graphics, etc.)
4. **ncr_dmatest.resource** ← Our module (at end)

## Size Comparison

| ROM | Size | Modules |
|-----|------|---------|
| Original | 512KB | 36 modules |
| Patched | 512KB | 30 modules |
| Removed | ~30KB | 7 modules |
| Added | 14KB | ncr_dmatest.resource |

## Next Steps

After creating the patched ROM:
1. Test in emulator first (Amiberry recommended)
2. Verify ncr_dmatest command works
3. Run DMA tests to verify functionality
4. If all works, flash to hardware ROM

## Support

See [ROM_MODULE.md](ROM_MODULE.md) for details on the ROM module structure and how it works.
