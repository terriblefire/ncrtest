/*
 * ncr_scsi.h - SCSI Command Tool Header
 * Based on Kickstart ROM NCR 53C710 driver
 */

#ifndef NCR_SCSI_H
#define NCR_SCSI_H

#include <exec/types.h>
#include "ncr_dmatest.h"  // For ncr710 structure

/* SCSI Command Codes (from ROM driver scsi.h) */
#define S_TEST_UNIT_READY	0x00
#define S_REQUEST_SENSE		0x03
#define S_INQUIRY		0x12
#define S_READ_CAPACITY		0x25
#define S_READ10		0x28
#define S_WRITE10		0x2a

/* SCSI block size (standard) */
#define SCSI_BLOCK_SIZE		512

/* Read parameters */
#define READ_32MB_SIZE		(32 * 1024 * 1024)	// 32MB
#define READ_32MB_BLOCKS	(READ_32MB_SIZE / SCSI_BLOCK_SIZE)  // 65536 blocks
#define READ_CHUNK_SIZE		(64 * 1024)		// 64KB per transfer
#define READ_CHUNK_BLOCKS	(READ_CHUNK_SIZE / SCSI_BLOCK_SIZE)  // 128 blocks

/* SCSI Status Codes */
#define SCSI_GOOD		0x00
#define SCSI_CHECK_CONDITION	0x02
#define SCSI_BUSY		0x08

/* SCSI Messages */
#define MSG_COMMAND_COMPLETE	0x00
#define MSG_SAVE_DATA_POINTER	0x02
#define MSG_RESTORE_POINTERS	0x03
#define MSG_DISCONNECT		0x04
#define MSG_ABORT		0x06
#define MSG_REJECT		0x07
#define MSG_NOP			0x08
#define MSG_IDENTIFY		0x80  // + LUN (0-7)

/* SCSI Control Register Bits (from ROM driver) */
// SCNTL0 bits
#define SCNTL0F_ARB1	(1<<7)
#define SCNTL0F_ARB0	(1<<6)
#define SCNTL0F_START	(1<<5)
#define SCNTL0F_WATN	(1<<4)
#define SCNTL0F_EPC	(1<<3)  // Enable Parity Checking
#define SCNTL0F_EPG	(1<<2)  // Enable Parity Generation
#define SCNTL0F_AAP	(1<<1)  // Assert ATN on Parity Error
#define SCNTL0F_TRG	(1<<0)  // Target mode

// SCNTL1 bits
#define SCNTL1F_EXC	(1<<7)  // Extra Clock Cycle
#define SCNTL1F_ADB	(1<<6)  // Assert Data Bus
#define SCNTL1F_ESR	(1<<5)  // Enable Selection/Reselection
#define SCNTL1F_CON	(1<<4)  // Connected
#define SCNTL1F_RST	(1<<3)  // Assert SCSI RST signal
#define SCNTL1F_AESP	(1<<2)  // Assert Even SCSI Parity
#define SCNTL1F_SND	(1<<1)  // Start Send
#define SCNTL1F_RCV	(1<<0)  // Start Receive

// SXFER bits
#define SXFERF_DHP	(1<<7)  // Disable Halt on Parity error

/* SCSI Chip ID (typically 7 for initiator) */
#define NCR_SCSI_ID	7

/* Table Indirect Move Data (from ROM driver ncr710.h) */
struct move_data {
	ULONG len;	// 24-bit length (high byte 0)
	ULONG addr;	// address for move
};

/* Select Data for Table Indirect (from ROM driver) */
struct SelectData {
	UBYTE res1;	// Reserved (0)
	UBYTE id;	// SCSI ID bitmask (1 << target_id)
	UBYTE sync;	// Sync transfer value
	UBYTE res2;	// Reserved (0)
};

/* DSA Entry - Data Structure Address (from ROM driver) */
/* This is what the SCRIPTS program uses via table-indirect addressing */
struct DSA_entry {
	struct move_data    move_data;		//  0  data move
	struct move_data    save_data;		//  8  saved data pointers
	UBYTE 		   *final_ptr;		// 16  final pointer
	struct SelectData   select_data;	// 20  selection data
	struct move_data    status_data;	// 24  1 byte to status
	struct move_data    recv_msg;		// 32  1 byte to message
	struct move_data    send_msg;		// 40  N byte send
	struct move_data    command_data;	// 48  command move
	UBYTE send_buf[16];			// 56  message + command buffer (expanded for READ(10))
	UBYTE recv_buf[8];			// 72  message in buffer
	UBYTE status_buf[1];			// 80  status byte
	UBYTE pad[3];				// 81  padding to longword
};

/* SCSI Command Request */
struct SCSICmd {
	UBYTE  *command;	// Pointer to SCSI command bytes
	UWORD   cmd_len;	// Command length (6, 10, or 12 bytes)
	UBYTE  *data;		// Data buffer
	ULONG   data_len;	// Data length
	UBYTE   target_id;	// SCSI ID (0-7)
	UBYTE   lun;		// Logical Unit Number (0-7)
	UBYTE   direction;	// 0=write to device, 1=read from device
	UBYTE   pad;
};

/* INQUIRY Response Structure (36 bytes minimum) */
struct InquiryData {
	UBYTE device_type;	// 0 = disk, 5 = CD-ROM, etc.
	UBYTE removable;	// bit 7 = removable
	UBYTE version;		// SCSI version
	UBYTE response_format;	// Response data format
	UBYTE additional_len;	// Additional length
	UBYTE reserved[3];
	UBYTE vendor[8];	// Vendor ID
	UBYTE product[16];	// Product ID
	UBYTE revision[4];	// Revision
};

/* Interrupt support */
#define NCR_INTNUM	3	// INTB_PORTS for A4000T NCR chip

/* Global interrupt state */
struct NCRIntState {
	struct Task *task;		// Task to signal
	ULONG signal_mask;		// Signal bit to send
	volatile UBYTE istat;		// Saved ISTAT value
	volatile UBYTE dstat;		// Saved DSTAT value
	volatile UBYTE sstat0;		// Saved SSTAT0 value
	volatile ULONG dsps;		// Saved DSPS value
	volatile LONG int_received;	// Flag: interrupt received
};

/* Function Prototypes */
LONG SetupNCRInterrupts(volatile struct ncr710 *ncr);
void CleanupNCRInterrupts(volatile struct ncr710 *ncr);
LONG InitNCRForSCSI(volatile struct ncr710 *ncr);
LONG DoInquiry(volatile struct ncr710 *ncr, UBYTE target_id, struct InquiryData *data);
void PrintInquiryData(struct InquiryData *data);
LONG DoRead32MB(volatile struct ncr710 *ncr, UBYTE target_id);
LONG DoGenerateFile(const char *filename);

#endif /* NCR_SCSI_H */
