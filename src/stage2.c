//#define QEMU 1

#include <stdint.h>
#include "ext2.h"
#include "heap.h"
#include "ide.h"
#include "io.h"
#include "lib.h"
#include "mbr.h"

void boot_seabios(uint32_t ramSize);

static volatile uint32_t bss_probe;					// must read 0: stage2_entry cleared .bss
static volatile uint32_t data_probe=0x600dc0de;		// must be intact: .data came through the ROM->RAM shadow copy

static ext2_fs_t rootfs;

void pci_assign_irq(uint8_t bus, uint8_t dev, uint8_t func, const uint8_t irq)
{
	uart_puts("Assigning IRQ ");
	uart_uint32(irq);
	uart_puts(" to ");
	uart_uint32(bus);
	uart_putc(':');
	uart_uint32(dev);
	uart_putc('.');
	uart_uint32(func);
	uart_putc('\n');

	// Set device's IRQ assignment
	pci_write8(PCI_DEV(bus, dev, func, 0x3c), irq);

	// Change to level triggered in PIC.
	uint16_t int_bits=inb(0x4d0)|(((uint16_t)inb(0x4d1))<<8);

	int_bits|=(1<<irq);

	// Write new values
	outb((uint8_t)(int_bits&0xff), 0x4d0);
	outb((uint8_t)(int_bits>>8), 0x4d1);
}

void stage2_main(uint32_t totalRAM)
{
	uart_puts("\nStage 2:\n");

	uart_puts("C runtime: .bss ");
	uart_puts(bss_probe==0 ? "zeroed" : "NOT ZERO");
	uart_puts(", .data ");
	uart_puts(data_probe==0x600dc0de ? "intact" : "BAD");
	uart_puts(", stack ok\n\n");

	// Southbridge USB IRQ
	pci_assign_irq(0x00, 0x11, 0x02, 10);

	// Southbridge ethernet IRQ
	pci_assign_irq(0x00, 0x12, 0x00, 11); // eth3 (4 port hub)

	// 3x RHINE-III IRQs
	pci_assign_irq(0x00, 0x0e, 0x00, 12); // eth0 (WAN2)
	pci_assign_irq(0x00, 0x0f, 0x00, 10); // eth1 (WAN1)
	pci_assign_irq(0x00, 0x10, 0x00, 9); // eth2 (DMZ)

	// VGA, not sure if this is correct
	pci_assign_irq(0x01, 0x00, 0x00, 11);

	uart_putc('\n');

#if 0
	if(!heap_init(totalRAM))
	{
		uart_puts("Heap initialized successfully.\n");
		uart_puts("Heap available: ");
		uart_uint32(heap_available());
		uart_puts(" bytes, largest free block: ");
		uart_uint32(heap_largest_free());
		uart_puts(" bytes\n");
	}
	else
		uart_puts("Heap initialization failed.\n");
	uart_putc('\n');

	// reserve a chunk of memory to space out the kernel load,
	// so they don't get wiped out when the kernel is set up
	// and copied into it's entry location.
	// (most likely destorying stack and anything else)
	void *reserved=heap_alloc(16*1024*1024);
	(void)reserved; // unused

	uint8_t *kernelImage=NULL;
	uint32_t kernelSize=0;

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

		if(ext2_mount(&rootfs, &ide_dev, partition.startLBA, partition.sectors)!=0)
			uart_puts("ext2 mount failed.\n");

		if(rootfs.mounted)
		{
			const char kernelName[]="/vmlinuz";

			if(ext2_read_file(&rootfs, kernelName, NULL, 0, &kernelSize)==0)
			{
				uart_puts("Found kernel: ");
				uart_puts(kernelName);
				uart_puts(" (");
				uart_uint32(kernelSize/1024);
				uart_puts("KB)\n");

				kernelImage=heap_alloc(kernelSize);

				if(kernelImage==NULL)
				{
					uart_puts("Failed to allocate memory for kernel image.\n");
					return;
				}

				if(ext2_read_file(&rootfs, kernelName, kernelImage, kernelSize, &kernelSize)!=0)
					uart_puts("Failed to read kernel image\n");
			}
			else
				uart_puts("Failed to get kernel image size\n");
		}
	}
	else
		uart_puts("IDE init failed or no device found.\n");

	uart_puts("Booting...\n");
	uart_int32(boot_linux(kernelImage, kernelSize, "console=ttyS0,115200 root=/dev/hda1 ro rootwait agp=off ide=nodma", totalRAM));
	uart_putc('\n');
#endif
	boot_seabios(totalRAM);

	uart_puts("Halted.\n");
	for (;;);
}
