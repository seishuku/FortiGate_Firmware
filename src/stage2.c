#include <stdint.h>
#include <stdio.h>
#include "io.h"
#include "ide.h"
#include "mbr.h"
#include "ext2.h"

extern char __reserved_lo[];   /* start of the region stage 2 itself uses (bss + stack), from link.ld */

static volatile uint32_t bss_probe;                    /* must read 0: stage2_entry cleared .bss */
static volatile uint32_t data_probe=0x600dc0de;      /* must be intact: .data came through the ROM->RAM shadow copy */

#define NB(r) PCI_DEV(0, 0, 0, (r))

void pci_scan(void)
{
    uart_puts("PCI Bus Scan Starting...\n");

    for (uint16_t bus = 0; bus < 256; bus++)
    {
        for (uint8_t device = 0; device < 32; device++)
        {
            // Check function 0 first.
            uint32_t id = pci_read32(PCI_DEV(bus, device, 0, 0x00));

            uint16_t vendor_id = id & 0xFFFF;
            uint16_t device_id = (id >> 16) & 0xFFFF;

            // 0xFFFF means no device present.
            if (vendor_id == 0xFFFF)
                continue;

            // Check header type to determine multifunction status.
            uint32_t header = pci_read32(PCI_DEV(bus, device, 0, 0x0C));
            uint8_t header_type = (header >> 16) & 0xFF;

            uint8_t max_functions = (header_type & 0x80) ? 8 : 1;

            for (uint8_t function = 0;function < max_functions;function++)
            {
                id = pci_read32(PCI_DEV(bus, device, function, 0x00));

                vendor_id = id & 0xFFFF;
                device_id = (id >> 16) & 0xFFFF;

                if (vendor_id == 0xFFFF)
                    continue;

                uart_puts("PCI Device: ");

                uart_puts("Bus=");
                uart_hex32(bus);

                uart_puts(" Device=");
                uart_hex32(device);

                uart_puts(" Function=");
                uart_hex32(function);

                uart_puts(" Vendor=");
                uart_hex32(vendor_id);

                uart_puts(" DeviceID=");
                uart_hex32(device_id);

                uart_puts("\n");
            }
        }
    }

    uart_puts("PCI Scan Complete.\n");
}

#define MBR_PARTITION_OFFSET 446
#define MBR_SIGNATURE_OFFSET 510

static uint32_t read_le32(const uint8_t *p)
{
	return (uint32_t)p[0]
		| ((uint32_t)p[1] << 8)
		| ((uint32_t)p[2] << 16)
		| ((uint32_t)p[3] << 24);
}

static uint16_t read_le16(const uint8_t *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void mbr_print(ide_dev_t *dev)
{
	uint8_t sector[512];
	int result;
	int i;

	uart_puts("Reading MBR...\n");

	result = ide_read(dev, 0, 1, sector);

	if(result != IDE_OK)
	{
		uart_puts("MBR read failed: ");
		uart_hex32((uint32_t)result);
		uart_putc('\n');
		return;
	}

	if(sector[510] != 0x55 || sector[511] != 0xAA)
	{
		uart_puts("Invalid MBR signature: ");
		uart_hex32(((uint32_t)sector[510] << 8) | sector[511]);
		uart_putc('\n');
		return;
	}

	uart_puts("MBR signature valid.\n");
	uart_puts("Primary partitions:\n");

	for(i = 0; i < 4; i++)
	{
		const uint8_t *p = &sector[MBR_PARTITION_OFFSET + i * 16];

		uint8_t status = p[0];
		uint8_t type = p[4];
		uint32_t start = read_le32(p + 8);
		uint32_t count = read_le32(p + 12);

		if(type == 0 || count == 0)
			continue;

		uart_puts("  Partition ");
		uart_hex32(i + 1);

		uart_puts(": type=");
		uart_hex32(type);

		uart_puts(" start=");
		uart_hex32(start);

		uart_puts(" sectors=");
		uart_hex32(count);

		uart_puts(" status=");
		uart_hex32(status);

		if((uint64_t)start + count > dev->sectors)
			uart_puts(" [OUT OF DISK RANGE]");

		uart_putc('\n');
	}
}

static void ext2_probe(ide_dev_t *dev, uint32_t partition_start)
{
	uint8_t sb[512];
	uint32_t block_size;
	uint32_t log_block_size;
	int result;

	uart_puts("Reading EXT superblock...\n");

	result = ide_read(dev, partition_start + 2, 1, sb);

	if(result != IDE_OK)
	{
		uart_puts("Superblock read failed: ");
		uart_hex32((uint32_t)result);
		uart_putc('\n');
		return;
	}

	if(read_le16(sb + 56) != 0xEF53)
	{
		uart_puts("No EXT filesystem magic: ");
		uart_hex32(read_le16(sb + 56));
		uart_putc('\n');
		return;
	}

	log_block_size = read_le32(sb + 24);

	if(log_block_size > 2)
	{
		uart_puts("Unsupported EXT block size exponent: ");
		uart_hex32(log_block_size);
		uart_putc('\n');
		return;
	}

	block_size = 1024U << log_block_size;

	uart_puts("EXT filesystem detected!\n");

	uart_puts("  Inodes: ");
	uart_hex32(read_le32(sb + 0));
	uart_putc('\n');

	uart_puts("  Blocks: ");
	uart_hex32(read_le32(sb + 4));
	uart_putc('\n');

	uart_puts("  Block size: ");
	uart_hex32(block_size);
	uart_putc('\n');

	uart_puts("  Blocks/group: ");
	uart_hex32(read_le32(sb + 32));
	uart_putc('\n');

	uart_puts("  Inodes/group: ");
	uart_hex32(read_le32(sb + 40));
	uart_putc('\n');

	uart_puts("  Revision: ");
	uart_hex32(read_le32(sb + 76));
	uart_putc('\n');

	uart_puts("  Inode size: ");
	uart_hex32(read_le16(sb + 88));
	uart_putc('\n');
}

static ext2_fs_t rootfs;

#if 0
extern unsigned char _binary_filo_elf_start[];
extern unsigned char _binary_filo_elf_end[];
extern unsigned char _binary_filo_elf_size[];

int elf_load(const void *image, uint32_t image_size, uint32_t ram_size, uint32_t *entry);
void cbtable_build(uint32_t ram_bytes);
#endif

void stage2_main(void)
{
	uart_puts("\nStage 2:\n\n");

	uart_puts("C runtime: .bss ");
	uart_puts(bss_probe==0 ? "zeroed" : "NOT ZERO");
	uart_puts(", .data ");
	uart_puts(data_probe==0x600dc0de ? "intact" : "BAD");
	uart_puts(", stack ok\n\n");

	uint32_t top=pci_read8(NB(0x5b))<<3;                /* NB Rx5B = end of last DRAM row, 8 MB units -> MB */
	uart_puts("Total RAM: ");
	uart_hex32(top);
	uart_puts(" MB (from as reported by northbridge)\n\n");

	pci_scan();

	ide_dev_t ide_dev;

	int result=ide_init(&ide_dev, 0x1F0, 0x3F6, 0);

	if(result==IDE_OK)
	{
		uart_puts("IDE device: ");
		if(ide_dev.present)
		{
			uart_puts("present, ");
			uart_puts(ide_dev.lba48 ? "LBA48" : "LBA28");
			uart_puts(", ");
			uart_puts(ide_dev.model);
			uart_puts(", ");
			uart_hex32((uint32_t)ide_dev.sectors);
			uart_puts(" sectors\n");
		}
		uart_puts("\n");

		mbr_print(&ide_dev);
		ext2_probe(&ide_dev, 2048);

		uart_puts("\n");

		mbr_partition_t partition;

		if(mbr_get_partition(&ide_dev, 1, &partition) != 0)
		{
			uart_puts("Failed to get partition 1 from MBR.\n");
			return;
		}

		if(ext2_mount(&rootfs, &ide_dev, partition.startLBA, partition.sectors) == 0)
			ext2_list_root(&rootfs);
	}
	else
		uart_puts("IDE init failed or no device found.\n");

#if 0
	uint32_t entry, detected_ram_size=top*1024*1024;

	void *filo_elf=_binary_filo_elf_start;
	uint32_t filo_size=_binary_filo_elf_end-_binary_filo_elf_start;

	int result=elf_load(filo_elf, filo_size, detected_ram_size, &entry);

	uart_puts("RAM size: ");
	uart_hex32(detected_ram_size);
	uart_putc('\n');

	if(result)
	{
		uart_puts("ELF loading failed!\n");
		uart_hex32(result);
		uart_putc('\n');
		for(;;);
	}

	uart_puts("building CBTABLE\n");
	cbtable_build(detected_ram_size);

	uart_puts("Calling FILO...\n");
	// calling FILO entry point here will crash real hardware,
	//     it runs in QEMU though despite incorrect hardware emulation.
	((void (*)(void))entry)();
#endif

	uart_puts("Halted.\n");
	for (;;);
}
