#include <stdint.h>
#include <stdio.h>
#include "io.h"
#include "ide.h"
#include "mbr.h"
#include "ext2.h"

extern char __reserved_lo[];   /* start of the region stage 2 itself uses (bss + stack), from link.ld */

static volatile uint32_t bss_probe;                    /* must read 0: stage2_entry cleared .bss */
static volatile uint32_t data_probe=0x600dc0de;      /* must be intact: .data came through the ROM->RAM shadow copy */

static ext2_fs_t rootfs;

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

int elf_load(const void *image, uint32_t image_size, uint32_t ram_size, uint32_t *entry);

void stage2_main(void)
{
	uart_puts("\nStage 2:\n\n");

	uart_puts("C runtime: .bss ");
	uart_puts(bss_probe==0 ? "zeroed" : "NOT ZERO");
	uart_puts(", .data ");
	uart_puts(data_probe==0x600dc0de ? "intact" : "BAD");
	uart_puts(", stack ok\n\n");

	uint32_t totalRAM=pci_read8(NB(0x5b))<<3;                /* NB Rx5B = end of last DRAM row, 8 MB units -> MB */
	uart_puts("Total RAM: ");
	uart_hex32(totalRAM);
	uart_puts(" MB (from as reported by northbridge)\n\n");
	totalRAM*=1024*1024; // to bytes;

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
		uart_puts("\n");

		mbr_partition_t partition;
		if(mbr_get_partition(&ide_dev, 1, &partition) != 0)
		{
			uart_puts("Failed to get partition 1 from MBR.\n");
			return;
		}

		if(ext2_mount(&rootfs, &ide_dev, partition.startLBA, partition.sectors) == 0)
			ext2_list_root(&rootfs);

		if(rootfs.mounted)
		{
			{
				const char filePath[]="/test/test.txt";
				uint8_t buffer[64]={ 0 };
				uint32_t bytesRead=0;

				// Read a test text file
				if(ext2_read_file(&rootfs, filePath, buffer, sizeof(buffer), &bytesRead) == 0)
				{
					uart_puts("\nContents of ");
					uart_puts(filePath);
					uart_puts(":\n");
					for(uint32_t i = 0; i < bytesRead; i++)
						uart_putc(buffer[i]);
					uart_putc('\n');
				}
			}

			{
				const char filePath[]="/hello.elf";
				uint8_t buffer[8192]={ 0 };
				uint32_t bytesRead=0, entryPoint=0;

				if(ext2_read_file(&rootfs, filePath, buffer, sizeof(buffer), &bytesRead) == 0)
				{
					elf_load(buffer, bytesRead, totalRAM, &entryPoint);
					uart_puts("\nELF file ");
					uart_puts(filePath);
					uart_puts(" loaded, entry point: ");
					uart_hex32(entryPoint);
					uart_puts("\n");

					// Jump to the entry point of the loaded ELF file
					void (*entryFunc)(void)=(void (*)(void))entryPoint;
					entryFunc();
				}
			}
		}
	}
	else
		uart_puts("IDE init failed or no device found.\n");

	uart_puts("Halted.\n");
	for (;;);
}
