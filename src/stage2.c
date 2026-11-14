//#define QEMU 1

#include <stdint.h>
#include "ext2.h"
#include "heap.h"
#include "ide.h"
#include "io.h"
#include "lib.h"
#include "mbr.h"

static volatile uint32_t bss_probe;					// must read 0: stage2_entry cleared .bss
static volatile uint32_t data_probe=0x600dc0de;		// must be intact: .data came through the ROM->RAM shadow copy

static ext2_fs_t rootfs;

#define NB(r) PCI_DEV(0, 0, 0, (r))

// Linux boot protocol constants
#define BOOT_MAGIC_HDRS     0x53726448  // "HdrS"
#define MIN_BOOT_VERSION    0x0202
#define LOADED_HIGH         0x01

#define BOOT_PARAMS_ADDR    0x00090000
#define CMDLINE_ADDR        0x00095000
#define GDT_ADDR            0x00096000
#define CMDLINE_MAX         255

#define BOOT_CS             0x10
#define BOOT_DS             0x18

#define E820_RAM            1
#define E820_RESERVED       2

typedef struct
{
    uint8_t  setupSectors;     // 0x1F1
    uint8_t  pad1[0x1F4-0x1F2];
    uint32_t syssize;         // 0x1F4
    uint8_t  pad2[0x202-0x1F8];
    uint32_t header;          // 0x202 "HdrS"
    uint16_t version;         // 0x206
    uint8_t  pad3[0x210-0x208];
    uint8_t  typeOfLoader;  // 0x210
    uint8_t  loadFlags;       // 0x211
    uint16_t setupMoveSize; // 0x212
    uint32_t code32Start;    // 0x214
    uint8_t  pad4[0x228-0x218];
    uint32_t cmdLinePtr;    // 0x228
} __attribute__((packed)) SetupHeader_t;

typedef struct
{
    uint64_t addr;
    uint64_t size;
    uint32_t type;
} __attribute__((packed)) E820_t;

typedef struct
{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) GDT_t;

// null, null, 0x10 = flat 4GB code, 0x18 = flat 4GB data.
// Accessed bits are pre-set (0x9B / 0x93) so the CPU never needs to write them back.
static const uint64_t bootGDT[4]=
{
    0x0000000000000000ull,
	0x0000000000000000ull,
    0x00CF9B000000FFFFull,  // __BOOT_CS
    0x00CF93000000FFFFull,  // __BOOT_DS
};

#ifdef QEMU
#define LAPIC_BASE   0xFEE00000u
#define LAPIC_SVR    0x0F0
#define LAPIC_LINT0  0x350
#define LAPIC_LINT1  0x360

static inline void lapic_write(uint32_t reg, uint32_t v)
{
	*(volatile uint32_t *)(LAPIC_BASE+reg)=v;
}

static inline uint32_t lapic_read(uint32_t reg)
{
	return *(volatile uint32_t *)(LAPIC_BASE+reg);
}

static void setup_virtual_wire(void)
{
	// Enable the LAPIC (SVR bit 8), spurious vector 0xFF
	lapic_write(LAPIC_SVR, (lapic_read(LAPIC_SVR)&~0xFFu)|0x100|0xFF);

	// LINT0 = ExtINT, unmasked: the 8259's INTR passes through to the CPU
	lapic_write(LAPIC_LINT0, 0x00000700);

	// LINT1 = NMI
	lapic_write(LAPIC_LINT1, 0x00000400);
}
#endif

int boot_linux(void *kernel_image_start, size_t total_image_size, const char *cmdline, uint32_t ram_size)
{
	uint8_t *img=(uint8_t *)kernel_image_start;

	SetupHeader_t *srcHdr=(SetupHeader_t *)(img+0x1F1);

	if(srcHdr->header!=BOOT_MAGIC_HDRS)
		return -1;
	if(srcHdr->version<MIN_BOOT_VERSION)
		return -2;
	if(!(srcHdr->loadFlags&LOADED_HIGH))
		return -3; // zImage: not supported here

	uint32_t setupSectors=srcHdr->setupSectors;

	if(setupSectors==0)
		setupSectors=4;

	size_t setupSize=(size_t)(setupSectors+1)*512;

	if(setupSize>=total_image_size)
		return -4;

	// Zero the zero page
	uint8_t *params=(uint8_t *)BOOT_PARAMS_ADDR;
	memset(params, 0, 4096);

	// header end = 0x202 + jump-instruction length byte at 0x201
	size_t hdrEnd=0x202+img[0x201];

	if(hdrEnd>0x280)
		hdrEnd=0x280;

	memcpy(params+0x1F1, img+0x1F1, hdrEnd-0x1F1);

	SetupHeader_t *hdr=(SetupHeader_t *)(params+0x1F1);

	hdr->typeOfLoader=0xFF;

	// Command line
	char *cmdDest=(char *)CMDLINE_ADDR;
	size_t n=0;

	if(cmdline)
	{
		while(cmdline[n]&&n<CMDLINE_MAX)
		{
			cmdDest[n]=cmdline[n];
			n++;
		}
	}

	cmdDest[n]=0;
	hdr->cmdLinePtr=CMDLINE_ADDR;

	// Console: VGA text mode 80x25
	params[0x00]=0;     // orig_x
	params[0x01]=24;    // orig_y (cursor at bottom)
	params[0x06]=3;     // orig_video_mode
	params[0x07]=80;    // orig_video_cols
	params[0x0E]=25;    // orig_video_lines
	params[0x0F]=1;     // orig_video_isVGA
	*(uint16_t *)(params+0x10)=16; // orig_video_points

	// Memory map
	uint8_t *e820Count=params+0x1E8;
	E820_t *e820=(E820_t *)(params+0x2D0);

	e820[0].addr=0x00000000;
	e820[0].size=0x00100000;
	e820[0].type=E820_RESERVED;

	e820[1].addr=0x00100000;
	e820[1].size=ram_size-0x00100000;
	e820[1].type=E820_RAM;

	*e820Count=2;

	// Legacy fields
	*(uint32_t *)(params+0x1E0)=(ram_size-0x00100000)>>10; // alt_mem_k

	// Copy kernel payload
	uint32_t entry=hdr->code32Start;
	memmove((void *)(uintptr_t)entry, img+setupSize, total_image_size-setupSize);

	// GDT into RAM
	memcpy((void *)GDT_ADDR, bootGDT, sizeof(bootGDT));
	static GDT_t gdt=
	{
		.limit=sizeof(bootGDT)-1,
		.base=GDT_ADDR,
	};

#ifdef QEMU
	// Needed for QEMU
	setup_virtual_wire();
#endif

	// Handoff: cli, load GDT, reload segments, set registers, jump
	__asm__ volatile(
	    "cli\n\t"
	    "lgdt (%%edx)\n"
	    "movl $0x18, %%eax\n"
	    "movw %%ax, %%ds\n"
	    "movw %%ax, %%es\n"
	    "movw %%ax, %%fs\n"
	    "movw %%ax, %%gs\n"
	    "movw %%ax, %%ss\n"
	    "ljmp $0x10, $1f\n"
	    "1:\n"
	    "cld\n"
	    "xorl %%ebp, %%ebp\n"
	    "xorl %%edi, %%edi\n"
	    "xorl %%ebx, %%ebx\n"
	    "jmp *%%ecx\n"
	    :
	    : "S"((uint32_t)BOOT_PARAMS_ADDR),
	      "c"(entry),
	      "d"(&gdt)
	    : "eax", "memory");

	__builtin_unreachable();
}

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

void stage2_main(void)
{
	uart_puts("\nStage 2:\n");

	uart_puts("C runtime: .bss ");
	uart_puts(bss_probe==0 ? "zeroed" : "NOT ZERO");
	uart_puts(", .data ");
	uart_puts(data_probe==0x600dc0de ? "intact" : "BAD");
	uart_puts(", stack ok\n");

	// Southbridge USB IRQ
	pci_assign_irq(0x00, 0x11, 0x02, 10);

	// Southbridge ethernet IRQ
	pci_assign_irq(0x00, 0x12, 0x00, 11); // eth3 (connected to 4 port hub)

	// 3x RHINE-III IRQs
	pci_assign_irq(0x00, 0x0e, 0x00, 12); // eth0 (WAN2)
	pci_assign_irq(0x00, 0x0f, 0x00, 10); // eth1 (WAN1)
	// Can't find a working good IRQ for this:
	//pci_assign_irq(0x00, 0x10, 0x00, 0); // eth2 (DMZ)

	uint32_t totalRAM=pci_read8(PCI_DEV(0, 0, 0, 0x5b))<<3;                /* NB Rx5B = end of last DRAM row, 8 MB units -> MB */
	uart_puts("Total RAM: ");
	uart_hex32(totalRAM);
	uart_puts(" MB (from as reported by northbridge)\n");
	totalRAM<<=20; // to bytes;

	if(!heap_init())
	{
		uart_puts("Heap initialized successfully.\n");
		uart_puts("Heap available: ");
		uart_hex32(heap_available());
		uart_puts(" bytes, largest free block: ");
		uart_hex32(heap_largest_free());
		uart_puts(" bytes\n");
	}
	else
		uart_puts("Heap initialization failed.\n");

	// reserve a chunk of memory to space out the kernel and initrd loads, so they don't get wiped out when the kernel is set up and copied into it's entry location.
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
	uart_int32(boot_linux(kernelImage, kernelSize, "console=ttyS0,115200 root=/dev/hda1 ro rootwait ide=nodma", totalRAM));
	uart_putc('\n');

	uart_puts("Halted.\n");
	for (;;);
}
