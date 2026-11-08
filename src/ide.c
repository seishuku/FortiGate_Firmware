/* Minimal polled ATA PIO driver for bare-metal bring-up.
 * No interrupts, no DMA, no timers: timeouts are poll-loop counts. */

#include "io.h" /* inb/inw/outb/outw (value first) and pci_read/write */
#include "ide.h"

/* Poll budgets. Each iteration is one slow I/O read (~1us on ISA-ish
 * buses), so these are roughly: 1s for BSY, 100ms for DRQ. Tune to taste. */
#define POLL_BSY			1000000
#define POLL_DRQ			100000
#define POLL_RESET			2000000
/* ---------------------------------------------------------------------- */

/* Register offsets from io base */
#define R_DATA				0
#define R_ERR				1	/* read */
#define R_FEAT				1	/* write */
#define R_COUNT				2
#define R_LBA0				3
#define R_LBA1				4
#define R_LBA2				5
#define R_DRIVE				6
#define R_STATUS			7	/* read */
#define R_CMD				7	/* write */

/* Status bits */
#define ST_ERR				0x01
#define ST_DRQ				0x08
#define ST_DF				0x20
#define ST_RDY				0x40
#define ST_BSY				0x80

/* Control reg bits */
#define CTL_NIEN			0x02
#define CTL_SRST			0x04
#define CTL_HOB				0x80

/* Commands */
#define CMD_READ_PIO		0x20
#define CMD_READ_PIO_EXT	0x24
#define CMD_WRITE_PIO		0x30
#define CMD_WRITE_PIO_EXT	0x34
#define CMD_FLUSH			0xE7
#define CMD_FLUSH_EXT		0xEA
#define CMD_IDENTIFY		0xEC

#define CHUNK				128 /* sectors per command */

/* ~400ns: four alt-status reads */
static void delay400(uint16_t ctrl)
{
	inb(ctrl);
	inb(ctrl);
	inb(ctrl);
	inb(ctrl);
}

/* Wait for BSY to clear. Returns final status or -1 on timeout.
 * Uses alt status so the pending interrupt isn't acked needlessly. */
static int32_t wait_not_busy(const ide_dev_t *d, int32_t loops)
{
	while(loops--)
	{
		uint8_t s=inb(d->ctrl);

		if(!(s&ST_BSY))
			return s;
	}

	return -1;
}

/* After a command: wait for BSY=0, then report DRQ / error. */
static int wait_drq(ide_dev_t *d)
{
	delay400(d->ctrl);

	int s=wait_not_busy(d, POLL_BSY);

	if(s<0)
		return IDE_ERR_TIMEOUT;

	if(s&(ST_ERR|ST_DF))
	{
		d->last_err=inb(d->io+R_ERR);
		return IDE_ERR_DEVICE;
	}

	for(int i=0;i<POLL_DRQ;i++)
	{
		s=inb(d->ctrl);

		if(s&(ST_ERR|ST_DF))
		{
			d->last_err=inb(d->io+R_ERR);
			return IDE_ERR_DEVICE;
		}

		if(!(s&ST_BSY)&&(s&ST_DRQ))
			return IDE_OK;
	}

	return IDE_ERR_TIMEOUT;
}

static int select_drive(ide_dev_t *d, uint8_t lba_hi4)
{
	outb(0xE0|(d->drive<<4)|(lba_hi4&0x0F), d->io+R_DRIVE);
	delay400(d->ctrl);

	int s=wait_not_busy(d, POLL_BSY);

	if(s<0)
		return IDE_ERR_TIMEOUT;

	/* wait for RDY too (not for the very first IDENTIFY; see ide_init) */
	return IDE_OK;
}

/* PCI */
int ide_pci_ports(uint8_t bus, uint8_t dev, uint8_t fn, int channel, uint16_t *io, uint16_t *ctrl)
{
	uint8_t progif=pci_read8(PCI_DEV(bus, dev, fn, 0x09));
	int native=channel?(progif&0x04):(progif&0x01);

	/* Make sure I/O space decoding is on */
	uint16_t cmd=pci_read16(PCI_DEV(bus, dev, fn, 0x04));

	if(!(cmd&0x0001))
		pci_write16(PCI_DEV(bus, dev, fn, 0x04), cmd | 0x0001);

	if(native)
	{
		uint32_t b0=pci_read32(PCI_DEV(bus, dev, fn, channel?0x18:0x10));
		uint32_t b1=pci_read32(PCI_DEV(bus, dev, fn, channel?0x1C:0x14));

		*io=(uint16_t)(b0&0xFFFC);
		*ctrl=(uint16_t)((b1&0xFFFC) + 2); /* BAR is 4 bytes, ctrl is reg 2 */
	}
	else
	{
		*io=channel?0x170:0x1F0;
		*ctrl=channel?0x376:0x3F6;
	}

	return (*io&&*ctrl)?IDE_OK:IDE_ERR_NODEV;
}

/* reset / identify */
int ide_reset(uint16_t io, uint16_t ctrl)
{
	outb(CTL_SRST|CTL_NIEN, ctrl);
	delay400(ctrl);
	delay400(ctrl); /* hold SRST a little longer, >5us total */
	outb(CTL_NIEN, ctrl);
	delay400(ctrl);

	/* Spec allows up to 31s for a spun-down drive; we wait much less. */
	for(int i=0;i<POLL_RESET;i++)
	{
		uint8_t s=inb(ctrl);

		if(!(s&ST_BSY))
			return IDE_OK;
	}

	(void)io;

	return IDE_ERR_TIMEOUT;
}

static void fix_string(char *dst, const uint16_t *w, int words)
{
	/* ATA strings are big-endian words, space padded */
	int n=0;

	for(int i=0;i<words;i++)
	{
		dst[n++]=(char)(w[i]>>8);
		dst[n++]=(char)(w[i]&0xFF);
	}

	while(n>0&&dst[n-1]==' ')
		n--;

	dst[n]='\0';
}

int ide_init(ide_dev_t *d, uint16_t io, uint16_t ctrl, int drive)
{
	d->io=io;
	d->ctrl=ctrl;
	d->drive=drive?1:0;
	d->present=0;
	d->lba48=0;
	d->last_err=0;
	d->sectors=0;
	d->model[0]='\0';

	outb(CTL_NIEN, d->ctrl); /* polling only */

	/* Floating bus check */
	outb(0xA0|(d->drive<<4), d->io+R_DRIVE);
	delay400(d->ctrl);

	if(inb(d->ctrl)==0xFF)
		return IDE_ERR_NODEV;

	/* Per spec: zero the address regs before IDENTIFY */
	outb(0, d->io+R_COUNT);
	outb(0, d->io+R_LBA0);
	outb(0, d->io+R_LBA1);
	outb(0, d->io+R_LBA2);
	outb(CMD_IDENTIFY, d->io+R_CMD);
	delay400(d->ctrl);

	if(inb(d->io+R_STATUS)==0)
		return IDE_ERR_NODEV;

	if(wait_not_busy(d, POLL_BSY)<0)
		return IDE_ERR_TIMEOUT;

	/* Non-zero LBA mid/hi after IDENTIFY = ATAPI (0x14/0xEB) or SATA (0x3C/0xC3) */
	if(inb(d->io+R_LBA1)||inb(d->io+R_LBA2))
		return IDE_ERR_ATAPI;

	int r=wait_drq(d);

	if(r!=IDE_OK)
		return r;

	uint16_t id[256];

	for(int i=0;i<256;i++)
		id[i]=inw(d->io+R_DATA);

	fix_string(d->model, &id[27], 20);

	d->lba48=(id[83]&(1<<10))?1:0;

	if(d->lba48)
		d->sectors=(uint64_t)id[100]|((uint64_t)id[101]<<16)|((uint64_t)id[102]<<32)|((uint64_t)id[103]<<48);
	else
		d->sectors=(uint32_t)id[60]|((uint32_t)id[61]<<16);

	if(d->sectors==0)
		return IDE_ERR_NODEV; /* no LBA support; CHS not handled */

	d->present=1;

	return IDE_OK;
}

/* ---- transfers ------------------------------------------------------- */
static int setup_cmd(ide_dev_t *d, uint64_t lba, uint32_t n, int write)
{
	if(d->lba48)
	{
		int r=select_drive(d, 0);

		if(r)
			return r;

		/* high-order bytes first, then low-order */
		outb((n>>8)&0xFF, d->io+R_COUNT);
		outb((lba>>24)&0xFF, d->io+R_LBA0);
		outb((lba>>32)&0xFF, d->io+R_LBA1);
		outb((lba>>40)&0xFF, d->io+R_LBA2);
		outb(n&0xFF, d->io+R_COUNT);
		outb(lba&0xFF, d->io+R_LBA0);
		outb((lba>>8)&0xFF, d->io+R_LBA1);
		outb((lba>>16)&0xFF, d->io+R_LBA2);
		outb(write?CMD_WRITE_PIO_EXT:CMD_READ_PIO_EXT, d->io+R_CMD);
	}
	else
	{
		int r=select_drive(d, (lba>>24)&0x0F);

		if(r)
			return r;

		outb(n&0xFF, d->io+R_COUNT); /* 0 would mean 256 */
		outb(lba&0xFF, d->io+R_LBA0);
		outb((lba>>8)&0xFF, d->io+R_LBA1);
		outb((lba>>16)&0xFF, d->io+R_LBA2);
		outb(write?CMD_WRITE_PIO:CMD_READ_PIO, d->io+R_CMD);
	}

	return IDE_OK;
}

static int check_range(ide_dev_t *d, uint64_t lba, uint32_t count)
{
	if(!d->present||count==0)
		return IDE_ERR_RANGE;

	if(lba+count>d->sectors)
		return IDE_ERR_RANGE;

	if(!d->lba48&&(lba+count)>0x10000000ULL)
		return IDE_ERR_RANGE;

	return IDE_OK;
}

int ide_read(ide_dev_t *d, uint64_t lba, uint32_t count, void *buf)
{
	int r=check_range(d, lba, count);

	if(r)
		return r;

	uint16_t *p=(uint16_t *)buf;

	while(count)
	{
		uint32_t n=count>CHUNK?CHUNK:count;

		r=setup_cmd(d, lba, n, 0);

		if(r)
			return r;

		for(uint32_t s=0;s<n;s++)
		{
			r=wait_drq(d);

			if(r)
				return r;

			for(int i=0;i<256;i++)
				*p++=inw(d->io+R_DATA);
		}

		lba+=n;
		count-=n;
	}

	return IDE_OK;
}

int ide_write(ide_dev_t *d, uint64_t lba, uint32_t count, const void *buf)
{
	int r=check_range(d, lba, count);

	if(r)
		return r;

	const uint16_t *p=(const uint16_t *)buf;

	while(count)
	{
		uint32_t n=count>CHUNK?CHUNK:count;

		r=setup_cmd(d, lba, n, 1);

		if(r)
			return r;

		for(uint32_t s=0;s<n;s++)
		{
			r=wait_drq(d);

			if(r)
				return r;

			for(int i=0;i<256;i++)
				outw(*p++, d->io+R_DATA);
		}
		lba+=n;
		count-=n;
	}

	/* Flush the drive's write cache */
	outb(d->lba48?CMD_FLUSH_EXT:CMD_FLUSH, d->io+R_CMD);
	delay400(d->ctrl);

	int s=wait_not_busy(d, POLL_BSY*4);

	if(s<0)
		return IDE_ERR_TIMEOUT;

	if(s&(ST_ERR|ST_DF))
	{
		d->last_err=inb(d->io+R_ERR);
		return IDE_ERR_DEVICE;
	}

	return IDE_OK;
}
