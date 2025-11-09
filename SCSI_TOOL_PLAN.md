# SCSI Command-Line Tool Plan

## Goal
Create a command-line tool that performs actual SCSI operations (like the Kickstart ROM driver) using the NCR 53C710, not just memory-to-memory DMA tests.

## Analysis of Kickstart ROM Driver

### Key Components

**1. NCR Driver (`ncr.c`)**
- Initializes NCR 53C710 chip
- Handles interrupts from NCR
- Manages SCSI state machine
- Uses SCRIPTS for DMA operations

**2. SCRIPTS Program (`script.c`)**
- Compiled SCRIPTS binary (SCSI I/O Processor instructions)
- Handles full SCSI protocol:
  - Selection
  - Command phase
  - Data phase (in/out)
  - Status phase
  - Message phase
  - Disconnection/Reconnection
  - Tagged command queuing

**3. SCSI Task (`scsitask.asm`)**
- Queues SCSI commands
- Manages multiple units (devices)
- Handles disconnects
- Threading of requests

**4. Command Structures**
- `CommandBlock` - Wraps IORequest
- `DSA` (Data Structure Address) - Points to scatter-gather data
- SCSI command descriptors

## What We Already Have

✅ NCR chip detection and initialization (`ncr_init.c`)
✅ Basic SCRIPTS execution (Memory Move + Interrupt)
✅ DMA engine testing
✅ Memory region handling

## What We Need to Add

### Phase 1: Basic SCSI Operations

**1. SCSI Command Structure**
```c
struct SCSICmd {
    UBYTE *command;        // SCSI command bytes
    UWORD command_length;  // Command length (6, 10, or 12 bytes)
    UBYTE *data;           // Data buffer
    ULONG data_length;     // Data transfer length
    UBYTE target_id;       // SCSI ID (0-7)
    UBYTE lun;             // Logical Unit Number
    UBYTE direction;       // 0=write to device, 1=read from device
};
```

**2. SCRIPTS for SCSI (not just memory moves)**

Need SCRIPTS that:
- Arbitrate for SCSI bus
- Select target device
- Send command bytes
- Transfer data (if any)
- Receive status byte
- Receive message byte
- Handle disconnects

**3. Basic SCSI Commands to Implement**

Priority order:
1. **INQUIRY** (0x12) - Identifies device, no data transfer complications
2. **TEST UNIT READY** (0x00) - Simple, no data
3. **REQUEST SENSE** (0x03) - Get error info
4. **READ CAPACITY** (0x25) - Get disk size
5. **READ(10)** (0x28) - Read sectors
6. **WRITE(10)** (0x2A) - Write sectors

### Phase 2: Command-Line Interface

```bash
# Usage examples:
ncr_scsi inquiry 3              # INQUIRY to SCSI ID 3
ncr_scsi read 3 0 1 output.bin  # Read LBA 0, 1 sector from ID 3
ncr_scsi write 3 0 1 input.bin  # Write LBA 0, 1 sector to ID 3
ncr_scsi capacity 3             # Get capacity of ID 3
```

## Implementation Strategy

### Simplest Starting Point: INQUIRY Command

**Why INQUIRY?**
- Most basic SCSI command
- No LUN/LBA addressing needed
- Fixed 36-byte response
- All SCSI devices must support it
- No write operations (safer for testing)

**INQUIRY Command Format:**
```
Byte 0: 0x12 (INQUIRY opcode)
Byte 1: 0x00 (LUN = 0)
Byte 2: 0x00 (Page code = 0)
Byte 3: 0x00 (Reserved)
Byte 4: 0x24 (Allocation length = 36 bytes)
Byte 5: 0x00 (Control)
```

**INQUIRY Response (36 bytes):**
```
Byte 0: Device type (0=disk, 5=CD-ROM, etc.)
Byte 1-7: Vendor ID
Byte 8-15: Product ID
Byte 16-23: Product Revision
...
```

### SCRIPTS Program for INQUIRY

```c
// Pseudo-SCRIPTS for INQUIRY
arbitrate:
    // Arbitrate for SCSI bus
    Wait for arbitration won

select:
    // Select target device with ATN
    Select target_id with ATN
    Jump to error if selection failed

message_out:
    // Send IDENTIFY message
    Move 1 byte from identify_msg when MSG_OUT

command:
    // Send INQUIRY command (6 bytes)
    Move 6 bytes from inquiry_cmd when COMMAND

data_in:
    // Receive INQUIRY data (36 bytes)
    Move 36 bytes to inquiry_buffer when DATA_IN

status:
    // Get status byte
    Move 1 byte to status_byte when STATUS

message_in:
    // Get message byte (usually COMMAND COMPLETE)
    Move 1 byte to message_byte when MSG_IN
    Clear ACK

disconnect:
    // Wait for disconnect
    Wait for disconnect

done:
    // Interrupt to signal completion
    Int 0xDEADBEEF
```

## File Structure

```
ncr_scsi/
├── ncr_scsi.c           - Main command-line tool
├── ncr_scsi_cmd.c       - SCSI command builders
├── ncr_scsi_script.c    - SCRIPTS programs for SCSI
├── ncr_scsi.h           - SCSI structures and constants
├── ncr_init.c           - NCR initialization (reuse from ncrtest)
├── ncr_dmatest.h        - NCR register definitions (reuse)
└── Makefile             - Build system
```

## Challenges

### 1. **SCRIPTS Complexity**
- Memory Move is simple (2 instructions)
- SCSI protocol requires ~20-30 instructions
- Must handle all SCSI phases correctly
- Need conditional jumps for phase matching

### 2. **Phase Mismatch Handling**
- Device may not follow expected phases
- Must detect and handle unexpected phases
- Need interrupt on phase mismatch

### 3. **Timing**
- SCSI bus has timing requirements
- Selection timeout
- Command timeout
- Data transfer timeout

### 4. **Multi-Device Support**
- Arbitration for shared bus
- Device priority
- Reselection handling

### 5. **Error Handling**
- SCSI check condition
- Sense data retrieval
- Hardware errors
- Bus reset

## Development Plan

### Step 1: Simple SCRIPTS Compiler
Create a way to build SCRIPTS programs (or use pre-compiled ones like the ROM)

### Step 2: INQUIRY Implementation
- Build INQUIRY SCRIPTS
- Send to device
- Parse response
- Print device info

### Step 3: Add More Commands
- TEST UNIT READY
- READ CAPACITY
- READ(10) for testing
- WRITE(10) (carefully!)

### Step 4: Error Handling
- Check status bytes
- REQUEST SENSE on errors
- Proper cleanup

### Step 5: CLI Polish
- Argument parsing
- Help messages
- Hex dumps
- Verbose mode

## References

**NCR 53C710 SCSI I/O Processor:**
- Programmer's Guide: `/Users/stephen/git/ncrtest/NCR_53C710_SCSI_IO_Processor_Oct90.txt`
- Chapter 3: Single-Tasking SCSI Example
- Chapter 7: Scatter-Gather
- Chapter 8: Initiator Scripts

**Kickstart ROM Driver:**
- `/Users/stephen/git/kickstart/scsidisk/ncr.c` - C driver code
- `/Users/stephen/git/kickstart/scsidisk/script.c` - Compiled SCRIPTS
- `/Users/stephen/git/kickstart/scsidisk/scsitask.asm` - Task management

**SCSI Specifications:**
- SCSI-2 command set
- Common commands: INQUIRY, READ, WRITE, etc.

## Success Criteria

✅ Tool can detect NCR 53C710
✅ Tool can send INQUIRY to SCSI device
✅ Tool can parse and display INQUIRY response
✅ Tool can read sectors from disk
✅ Tool handles errors gracefully
✅ Tool works on real A4000T hardware

## Next Steps

1. **Extract SCRIPTS from ROM** - Analyze `script.c` to understand format
2. **Create minimal INQUIRY SCRIPTS** - Simplest possible version
3. **Test on real hardware** - Use existing SCSI devices
4. **Iterate and expand** - Add more commands

Would you like to start with implementing INQUIRY?
