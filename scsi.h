/*
 * SCSI Protocol Definitions
 * Separated from NCR hardware-specific code
 */

#ifndef SCSI_H
#define SCSI_H

#include <exec/types.h>

/* SCSI Command Opcodes */
#define SCSI_TEST_UNIT_READY    0x00
#define SCSI_REZERO_UNIT        0x01
#define SCSI_REQUEST_SENSE      0x03
#define SCSI_FORMAT_UNIT        0x04
#define SCSI_READ6              0x08
#define SCSI_WRITE6             0x0A
#define SCSI_INQUIRY            0x12
#define SCSI_MODE_SELECT        0x15
#define SCSI_MODE_SENSE         0x1A
#define SCSI_START_STOP_UNIT    0x1B
#define SCSI_READ_CAPACITY      0x25
#define SCSI_READ10             0x28
#define SCSI_WRITE10            0x2A

/* SCSI Status Codes */
#define SCSI_STATUS_GOOD        0x00
#define SCSI_STATUS_CHECK       0x02
#define SCSI_STATUS_BUSY        0x08

/* SCSI Message Codes */
#define SCSI_MSG_COMMAND_COMPLETE  0x00
#define SCSI_MSG_EXTENDED          0x01
#define SCSI_MSG_SAVE_POINTERS     0x02
#define SCSI_MSG_RESTORE_POINTERS  0x03
#define SCSI_MSG_DISCONNECT        0x04
#define SCSI_MSG_ABORT             0x06
#define SCSI_MSG_MESSAGE_REJECT    0x07
#define SCSI_MSG_NOP               0x08
#define SCSI_MSG_IDENTIFY          0x80  /* OR with LUN */

/* SCSI Phases (from SBCL/SSTAT1 register) */
#define SCSI_PHASE_DATA_OUT     0
#define SCSI_PHASE_DATA_IN      1
#define SCSI_PHASE_COMMAND      2
#define SCSI_PHASE_STATUS       3
#define SCSI_PHASE_MSG_OUT      6
#define SCSI_PHASE_MSG_IN       7

/* SCSI Sense Keys */
#define SENSE_NO_SENSE          0x00
#define SENSE_RECOVERED_ERROR   0x01
#define SENSE_NOT_READY         0x02
#define SENSE_MEDIUM_ERROR      0x03
#define SENSE_HARDWARE_ERROR    0x04
#define SENSE_ILLEGAL_REQUEST   0x05
#define SENSE_UNIT_ATTENTION    0x06
#define SENSE_DATA_PROTECT      0x07
#define SENSE_ABORTED_COMMAND   0x0B

/*
 * SCSI Command Data Structures
 */

/* 6-byte READ command (SCSI-1) */
struct scsi_read6_cmd {
	UBYTE opcode;           /* 0x08 */
	UBYTE lba_high;         /* LBA bits 20-16 + LUN bits 7-5 */
	UBYTE lba_mid;          /* LBA bits 15-8 */
	UBYTE lba_low;          /* LBA bits 7-0 */
	UBYTE length;           /* Transfer length in blocks */
	UBYTE control;          /* Control byte */
} __attribute__((packed));

/* 10-byte READ command (SCSI-2) */
struct scsi_read10_cmd {
	UBYTE opcode;           /* 0x28 */
	UBYTE flags;            /* Flags including LUN */
	UBYTE lba[4];           /* LBA as 32-bit big-endian */
	UBYTE reserved;
	UBYTE length[2];        /* Transfer length as 16-bit big-endian */
	UBYTE control;
} __attribute__((packed));

/* TEST_UNIT_READY command */
struct scsi_test_unit_ready_cmd {
	UBYTE opcode;           /* 0x00 */
	UBYTE reserved1;
	UBYTE reserved2;
	UBYTE reserved3;
	UBYTE reserved4;
	UBYTE control;
} __attribute__((packed));

/* INQUIRY command */
struct scsi_inquiry_cmd {
	UBYTE opcode;           /* 0x12 */
	UBYTE flags;            /* LUN in bits 7-5 */
	UBYTE page_code;
	UBYTE reserved;
	UBYTE alloc_length;
	UBYTE control;
} __attribute__((packed));

/* INQUIRY response data */
struct scsi_inquiry_data {
	UBYTE device_type;      /* Peripheral device type */
	UBYTE rmb;              /* Removable media bit */
	UBYTE version;
	UBYTE response_format;
	UBYTE additional_length;
	UBYTE reserved[3];
	UBYTE vendor[8];
	UBYTE product[16];
	UBYTE revision[4];
} __attribute__((packed));

/*
 * SCSI Operation Result Codes
 */
#define SCSI_OK                  0      /* Command completed successfully */
#define SCSI_ERR_SELECTION      -1      /* Selection timeout - no device */
#define SCSI_ERR_PHASE          -2      /* Unexpected phase */
#define SCSI_ERR_TIMEOUT        -3      /* Command timeout */
#define SCSI_ERR_CHECK          -4      /* CHECK CONDITION status */
#define SCSI_ERR_BUSY           -5      /* Device busy */
#define SCSI_ERR_DISCONNECT     -6      /* Unexpected disconnect */

#endif /* SCSI_H */
