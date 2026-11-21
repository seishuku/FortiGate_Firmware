#include <stdint.h>
#include <stddef.h>
#include "lib.h"

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

	// IVT/BIOS data
	e820[0].addr=0x00000000;
	e820[0].size=0x00001000;
	e820[0].type=E820_RESERVED;

	// Conventional RAM
	e820[1].addr=0x00001000;
	e820[1].size=0x0009e000;
	e820[1].type=E820_RAM;

	// EBDA
	e820[2].addr=0x0009F000;
	e820[2].size=0x00061000;
	e820[2].type=E820_RESERVED;

	// Extended RAM
	e820[3].addr=0x00100000;
	e820[3].size=ram_size-0x00100000;
	e820[3].type=E820_RAM;

	// Reserve the top 4MB for UMA
	// TODO: Detect this from northbridge registers?
	e820[4].addr=ram_size; // ram_size already has the UMA subtracted from it due to RAM test
	e820[4].size=0x00400000;
	e820[4].type=E820_RESERVED;

	*e820Count=5;

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
