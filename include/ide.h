#ifndef IDE_H
#define IDE_H

#include <stdint.h>

#define IDE_OK 0
#define IDE_ERR_TIMEOUT -1
#define IDE_ERR_DEVICE -2 /* ERR/DF set in status; see dev->last_err */
#define IDE_ERR_NODEV -3
#define IDE_ERR_ATAPI -4 /* ATAPI/SATA signature, not handled here */
#define IDE_ERR_RANGE -5

typedef struct
{
	uint16_t io;   /* command block base (0x1F0 / 0x170 / BAR) */
	uint16_t ctrl; /* control block base (0x3F6 / 0x376 / BAR+2) */
	uint8_t drive; /* 0 = master, 1 = slave */
	uint8_t present;
	uint8_t lba48;
	uint8_t last_err; /* error register from last failure */
	uint64_t sectors;
	char model[41];
} ide_dev_t;

/* Work out port bases for a channel (0 = primary, 1 = secondary) of a PCI
 * IDE controller. Handles legacy-mode and native-mode channels. */
int ide_pci_ports(uint8_t bus, uint8_t dev, uint8_t fn, int channel, uint16_t *io, uint16_t *ctrl);

/* Soft reset the channel (both drives). */
int ide_reset(uint16_t io, uint16_t ctrl);

/* Probe + IDENTIFY a drive. Fills in *d. Returns IDE_OK if usable. */
int ide_init(ide_dev_t *d, uint16_t io, uint16_t ctrl, int drive);

/* 512-byte sector PIO read/write. count may be any size >= 1. */
int ide_read(ide_dev_t *d, uint64_t lba, uint32_t count, void *buf);
int ide_write(ide_dev_t *d, uint64_t lba, uint32_t count, const void *buf);

#endif
