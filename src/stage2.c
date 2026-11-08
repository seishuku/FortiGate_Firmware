#include <stdint.h>
#include <stdio.h>
#include "io.h"

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

extern unsigned char _binary_filo_elf_start[];
extern unsigned char _binary_filo_elf_end[];
extern unsigned char _binary_filo_elf_size[];

int elf_load(const void *image, uint32_t image_size, uint32_t ram_size, uint32_t *entry);
void cbtable_build(uint32_t ram_bytes);

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

	uart_puts("Halted.\n");
	for (;;);
}
