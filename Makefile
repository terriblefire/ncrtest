#
# Makefile for NCR 53C710 DMA Test Tool
#
# This builds a ROM module for Amiga kickstart
#

# Compiler and tools
CC = m68k-amigaos-gcc

# Compiler flags for standard executable
CFLAGS = -O2 -m68040 -noixemul -fomit-frame-pointer
CFLAGS += -Wall -Wno-pointer-sign
CFLAGS += -DNCR53C710=1 -DIS_A4000T=1

# Linker flags for standard executable
LDFLAGS = -noixemul

# Source files - no assembly for standard executable
C_SRCS = main.c ncr_init.c ncr_dmatest.c

# Object files
C_OBJS = $(C_SRCS:.c=.o)
OBJS = $(C_OBJS)

# Output
TARGET = ncr_dmatest

# Default target
all: $(TARGET)

# Link the executable
$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)
	@echo "Executable built: $(TARGET)"
	@m68k-amigaos-size $(TARGET)

# Compile C files
%.o: %.c ncr_dmatest.h
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -f $(OBJS) $(TARGET) *.rom
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

.PHONY: all clean rebuild show kickstart distclean verify-kickstart
