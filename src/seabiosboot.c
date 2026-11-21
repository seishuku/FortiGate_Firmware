#include <stdint.h>
#include <stddef.h>
#include "io.h"
#include "lib.h"

int elf_load(const void *image, uint32_t image_size, uint32_t ram_size, uint32_t *entry);

extern const uint8_t _binary_seabios_elf_start[];
extern const uint8_t _binary_seabios_elf_end[];

extern const uint8_t _binary_vga_1023_8500_rom_start[];
extern const uint8_t _binary_vga_1023_8500_rom_end[];

#define CB_TAG_MEMORY	0x0001
#define CB_TAG_SERIAL	0x000f

#define CB_MEM_RAM		1
#define CB_MEM_RESERVED	2

typedef struct
{
    uint8_t  signature[4];   /* "LBIO" */
    uint32_t headerBytes;
    uint32_t headerChecksum;
    uint32_t tableBytes;
    uint32_t tableChecksum;
    uint32_t tableEntries;
} __attribute__((packed)) cb_header_t;

typedef struct
{
    uint32_t tag;
    uint32_t size;
    struct
	{
		uint64_t start;
		uint64_t size;
		uint32_t type;
	} __attribute__((packed)) map[5];
} __attribute__((packed)) cb_memory;

typedef struct
{
    uint32_t tag;
    uint32_t size;
    uint32_t type;
    uint32_t baseAddr;
    uint32_t baud;
    uint32_t regWidth;
    uint32_t inputHertz;
    uint32_t uartPCIAddr;
} __attribute__((packed)) cb_serial;

typedef struct
{
    cb_header_t hdr;
    cb_memory mem;
    cb_serial ser;
} __attribute__((packed)) minimal_cb_table;

static uint16_t cb_checksum(const void *ptr, size_t len)
{
    const uint8_t *p=(const uint8_t *)ptr;
    uint32_t sum=0;

    for(size_t i=0;i<len;i+=2)
	{
        uint16_t word=p[i];

		if(i+1<len)
            word|=((uint16_t)p[i+1]<<8);

		sum+=word;
    }

	while(sum>>16)
		sum=(sum&0xFFFF)+(sum>>16);

	return (uint16_t)(~sum);
}

void write_coreboot_table(uint64_t total_ram_bytes) {
    uintptr_t table_addr=0x00000500;
    __asm__ __volatile__("" : "+r"(table_addr));

	minimal_cb_table *tbl=(minimal_cb_table *)table_addr;

    tbl->mem.tag=CB_TAG_MEMORY;
    tbl->mem.size=sizeof(cb_memory);

	// IVT/BIOS data
	tbl->mem.map[0].start=0x00000000;
	tbl->mem.map[0].size=0x00001000;
	tbl->mem.map[0].type=CB_MEM_RESERVED;

	// Conventional RAM
	tbl->mem.map[1].start=0x00001000;
	tbl->mem.map[1].size=0x0009e000;
	tbl->mem.map[1].type=CB_MEM_RAM;

	// EBDA
	tbl->mem.map[2].start=0x0009F000;
	tbl->mem.map[2].size=0x00061000;
	tbl->mem.map[2].type=CB_MEM_RESERVED;

	// Extended RAM
	tbl->mem.map[3].start=0x00100000;
	tbl->mem.map[3].size=total_ram_bytes-0x00100000;
	tbl->mem.map[3].type=CB_MEM_RAM;

	// Reserve the top 4MB for UMA
	// TODO: Detect this from northbridge registers?
	tbl->mem.map[4].start=total_ram_bytes; // ramSize already has the UMA subtracted from it due to RAM test
	tbl->mem.map[4].size=0x00400000;
	tbl->mem.map[4].type=CB_MEM_RESERVED;

    // Serial port
    tbl->ser.tag=CB_TAG_SERIAL;
    tbl->ser.size=sizeof(cb_serial);
    tbl->ser.type=1;
    tbl->ser.baseAddr=0x3F8;
    tbl->ser.baud=115200;
    tbl->ser.regWidth=1;
    tbl->ser.inputHertz=1843200;
    tbl->ser.uartPCIAddr=0;

    // Header and checksums
    uint32_t recordsSize=sizeof(cb_memory)+sizeof(cb_serial);

    tbl->hdr.signature[0]='L';
    tbl->hdr.signature[1]='B';
    tbl->hdr.signature[2]='I';
    tbl->hdr.signature[3]='O';
    tbl->hdr.headerBytes=sizeof(cb_header_t);
    tbl->hdr.headerChecksum=0;
    tbl->hdr.tableBytes=recordsSize;
    tbl->hdr.tableChecksum=cb_checksum(&tbl->mem, recordsSize);
    tbl->hdr.tableEntries=2;

    tbl->hdr.headerChecksum=cb_checksum(&tbl->hdr, sizeof(cb_header_t));
}

void unlock_shadow_ram(void)
{
    uint32_t id=pci_read32(PCI_DEV(0, 0, 0, 0x00));
    uint16_t vendor=id&0xFFFF;
    uint16_t device=(id>>16)&0xFFFF;

    if(vendor==0x8086)
	{
        if(device==0x1237)
		{
            /* Intel 440FX (QEMU default -M pc): PAM0..PAM6 at 0x59..0x5F */
            pci_write8(PCI_DEV(0, 0, 0, 0x59), 0x30); /* 0xF0000-0xFFFFF: R/W */

			for(uint8_t reg=0x5A;reg<=0x5E;reg++)
				pci_write8(PCI_DEV(0, 0, 0, reg), 0x33); /* 0xC0000-0xEFFFF: R/W */
		}
		else if(device==0x29C0)
		{
            /* Intel Q35 (QEMU -M q35): PAM0..PAM6 at 0x90..0x96 */
            pci_write8(PCI_DEV(0, 0, 0, 0x90), 0x30); /* 0xF0000-0xFFFFF: R/W */

			for(uint8_t reg=0x91;reg<=0x95;reg++)
				pci_write8(PCI_DEV(0, 0, 0, reg), 0x33); /* 0xC0000-0xEFFFF: R/W */
        }
    }
	else if(vendor==0x1106)
	{
        /* VIA VT8601 (Apollo PLE133): Shadow RAM Control 1-3 at 0x61..0x63 */
        pci_write8(PCI_DEV(0, 0, 0, 0x61), 0xFF); /* 0xC0000-0xCFFFF: R/W */
        pci_write8(PCI_DEV(0, 0, 0, 0x62), 0xFF); /* 0xD0000-0xDFFFF: R/W */
        pci_write8(PCI_DEV(0, 0, 0, 0x63), 0xF0); /* 0xE0000-0xFFFFF: R/W (bits [7:4]) */
    }
}

void boot_seabios(uint32_t ramSize)
{
    uint32_t entry=0;
    uint32_t imageSize=(uint32_t)(_binary_seabios_elf_end-_binary_seabios_elf_start);

    write_coreboot_table(ramSize);
	unlock_shadow_ram();

	uint32_t vgaImageSize=(uint32_t)(_binary_vga_1023_8500_rom_end-_binary_vga_1023_8500_rom_start);
	memcpy((void *)0x000C0000, _binary_vga_1023_8500_rom_start, vgaImageSize);

	int result=elf_load(_binary_seabios_elf_start, imageSize, ramSize, &entry);

    if(result!=0)
	{
		uart_puts("elf_load: ");
		uart_int32(result);
		uart_putc('\n');
        return;
	}

	__asm__ __volatile__("cli");
    
    void (*seabios_entry)(void) = (void (*)(void))(uintptr_t)entry;
    seabios_entry();
}
