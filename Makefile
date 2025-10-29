#
# Makefile for NCR 53C710 DMA Test Tool
#
# This builds a ROM module for Amiga kickstart
#

# VBCC environment
export VBCC = /opt/vbcc

# Compiler and tools
CC = m68k-amigaos-gcc
VC = $(VBCC)/bin/vc
AS = $(VBCC)/bin/vasmm68k_mot
LD = $(VBCC)/bin/vlink

# Generate build date in dd.mm.yyyy format
BUILD_DATE := $(shell date +%d.%m.%Y)

# Compiler flags for standard executable (gcc)
CFLAGS = -O2 -m68040 -noixemul -fomit-frame-pointer
CFLAGS += -Wall -Wno-pointer-sign
CFLAGS += -DNCR53C710=1 -DIS_A4000T=1
CFLAGS += -DBUILD_DATE=\"$(BUILD_DATE)\"

# Compiler flags for ROM module (vbcc)
CFLAGS_ROM = -v -O2 -size -cpu=68040 -fastcall -nostdlib -c99 -k -sc +aos68k
CFLAGS_ROM += -I$(VBCC)/targets/m68k-amigaos/include -I$(VBCC)/NDK_3.9/Include/include_h
CFLAGS_ROM += -DBUILD_DATE=$(BUILD_DATE)

# Linker flags for standard executable
LDFLAGS = -noixemul

# Linker flags for ROM module
LDFLAGS_ROM = -sc -Bstatic -Cvbcc -nostdlib -Rshort -b amigahunk -s

# Assembler flags
ASFLAGS = -quiet -Fhunk -kick1hunks -nosym -m68040 -no-opt

# Source files for standard executable
C_SRCS = main.c ncr_init.c ncr_dmatest.c

# Source files for ROM module
ROM_C_SRCS = rom_resident.c rom_main.c
ROM_ASM_SRCS = rom_payload.s

# Object files
C_OBJS = $(C_SRCS:.c=.o)
ROM_C_OBJS = $(ROM_C_SRCS:.c=.o)
ROM_ASM_OBJS = $(ROM_ASM_SRCS:.s=.o)
ROM_OBJS = $(ROM_C_OBJS) $(ROM_ASM_OBJS)

# Output
TARGET = ncr_dmatest
ROM_TARGET = ncr_dmatest.resource

# Default target - build both
all: $(TARGET) $(ROM_TARGET)

# Build just the standard executable (for CI/CD without vbcc)
$(TARGET): $(C_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(C_OBJS)
	@echo "Executable built: $(TARGET)"
	@m68k-amigaos-size $(TARGET)

# Alias for building just the executable
ncr_dmatest: $(TARGET)

# Link the ROM module (vbcc)
$(ROM_TARGET): $(TARGET) $(ROM_OBJS)
	$(LD) $(LDFLAGS_ROM) $(ROM_OBJS) -o $@
	@echo "ROM module built: $(ROM_TARGET)"
	@ls -lh $(ROM_TARGET)

# Compile C files for standard executable (gcc)
main.o: main.c ncr_dmatest.h
	$(CC) $(CFLAGS) -c $< -o $@

ncr_init.o: ncr_init.c ncr_dmatest.h
	$(CC) $(CFLAGS) -c $< -o $@

ncr_dmatest.o: ncr_dmatest.c ncr_dmatest.h
	$(CC) $(CFLAGS) -c $< -o $@

# Compile C files for ROM module (vbcc)
rom_resident.o: rom_resident.c
	$(VC) $(CFLAGS_ROM) -c $< -o $@

rom_main.o: rom_main.c
	$(VC) $(CFLAGS_ROM) -c $< -o $@

# Assemble ROM payload (embeds the gcc-built executable)
rom_payload.o: rom_payload.s $(TARGET)
	$(AS) $(ASFLAGS) -o $@ $<

# Clean build artifacts
clean:
	rm -f $(C_OBJS) $(ROM_OBJS) $(TARGET) $(ROM_TARGET) *.rom *.asm
	@echo "Clean complete"

# Clean everything including ROM splits and venv
distclean: clean
	@echo "Deep clean complete"

# Rebuild everything
rebuild: clean all

# Show variables (for debugging the makefile)
show:
	@echo "CC       = $(CC)"
	@echo "AS       = $(AS)"
	@echo "LD       = $(LD)"
	@echo "CFLAGS   = $(CFLAGS)"
	@echo "ASFLAGS  = $(ASFLAGS)"
	@echo "LDFLAGS  = $(LDFLAGS)"
	@echo "C_SRCS   = $(C_SRCS)"
	@echo "ASM_SRCS = $(ASM_SRCS)"
	@echo "OBJS     = $(OBJS)"
	@echo "TARGET   = $(TARGET)"

.PHONY: all clean rebuild show distclean
