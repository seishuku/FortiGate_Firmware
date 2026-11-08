#include <stdint.h>
#include "io.h"

#define CR0_NW              0x20000000
#define CR0_CD              0x40000000

#define MSR_MTRR_PHYS_BASE0 0x200
#define MSR_MTRR_PHYS_MASK0 0x201
#define MSR_MTRR_FIX64K_00000 0x250
#define MSR_MTRR_FIX16K_80000 0x258
#define MSR_MTRR_FIX16K_A0000 0x259
#define MSR_MTRR_FIX4K_C0000  0x268     /* ... through 0x26f (F8000) */
#define MSR_MTRR_FIX4K_F8000  0x26f
#define MSR_MTRR_DEF_TYPE   0x2ff

#define MTRR_VAR_PAIRS      8
#define MTRR_TYPE_WB        0x06
#define MTRR_FIXED_ALL_WT   0x04040404
#define MTRR_MASK_VALID     0x800
#define MTRR_PHYS_MASK_HI   0x0000000f  /* 36-bit physical address space */
#define MTRR_DEF_FE         (1 << 10)
#define MTRR_DEF_E          (1 << 11)

#define NB(r)	PCI_DEV(0, 0x00, 0, (r))	/* VT8601 north bridge,		0:0.0	*/
#define SB0(r)	PCI_DEV(0, 0x11, 0, (r))	/* VT8231 PCI-ISA bridge,	0:17.0	*/
#define SB1(r)	PCI_DEV(0, 0x11, 1, (r))	/* VT8231 PCI-IDE bridge,	0:17.1	*/
#define SB4(r)	PCI_DEV(0, 0x11, 4, (r))	/* VT8231 power mgmt,		0:17.4	*/

/* ---- misc ---- */
static void delay_post(void)
{
	uint32_t i;

	for(i=0;i<4096;i++)
		__builtin_inb(0x80);
}

static void delay500(void)
{
	uint32_t i;

	for(i=0;i<500;i++)
		__builtin_outb(0, 0x80);
}

static uint32_t mem_read32(uint32_t a)
{
	return *(volatile uint32_t *)a;
}

static void mem_write32(uint32_t a, uint32_t v)
{
	*(volatile uint32_t *)a=v;
}

static void uart_init(void)
{
    __builtin_outb(0x83, 0x3fb);                      /* DLAB on */
	__builtin_outb(0x01, 0x3f8);                      /* divisor low */
    __builtin_outb(0x00, 0x3f9);                      /* divisor high */
    __builtin_outb(0x03, 0x3fb);                      /* 8N1, DLAB off */
    __builtin_outb(0x03, 0x3fc);                      /* DTR|RTS */
    __builtin_outb(0x00, 0x3f9);                      /* IER off */
    __builtin_outb(0x01, 0x3fa);                      /* FIFO on */
    __builtin_outb(0x07, 0x3fa);                      /* FIFO on + clear */
}

/* ---- south bridge (0:17.0) + its integrated Super-I/O config @0x3F0/0x3F1 ---- */
static void sio(uint8_t idx, uint8_t val)
{
	__builtin_outb(idx, 0x3f0);
	__builtin_outb(val, 0x3f1);
}

static void sb_prologue(void)
{
	pci_write8(SB0(0x43), 0xe0);

	pci_write8(SB0(0x50), 0xf6);

	sio(0xf2, 0x04);
	sio(0xf3, 0x00);
	sio(0xf4, 0xfe);
	sio(0xf6, 0xde);
	sio(0xf7, 0x00);
    sio(0xf2, 0x04);
	sio(0xfa, 0x00);
	sio(0xfb, 0x00);
	sio(0xfc, 0x00);

	uint8_t read=pci_read8(SB0(0x6c));
	pci_write8 (SB0(0x6c), read|0x80); // internal IO decode

	read=pci_read8(SB0(0x40));
	pci_write8 (SB0(0x40), read|0x44); // IO recovery time
	read=pci_read8(SB0(0x42));
	pci_write8 (SB0(0x42), read|0xf0); // something award does?
	read=pci_read8(SB0(0x4a));
	pci_write8 (SB0(0x4a), read|0x08); // dummy PCI request
	read=pci_read8(SB0(0x4c));
	pci_write8 (SB0(0x4c), (read&~0x0f)|0x04); // set default IDE interrupt values
	read=pci_read8(SB0(0x4f));
	pci_write8 (SB0(0x4f), read|0x08); // INIT# as CPU reset
	pci_write8 (SB0(0x50), 0xf2);
	read=pci_read8(SB0(0x51));
	pci_write8 (SB0(0x51), read|0x18);
	pci_write32(SB0(0x54), 0xc0a0b000);
	pci_write32(SB0(0x58), 0x00000003);
	pci_write32(SB0(0x5c), 0x40f00000);
	pci_write32(SB0(0x60), 0x00000000);
	pci_write32(SB0(0x64), 0x08020000);
	pci_write32(SB0(0x68), 0x00000001);

	// IDE
	read=pci_read8(SB1(0x40));
	pci_write8(SB1(0x40), read|0x03);
	// Enable prefetch buffers
	read=pci_read8(SB1(0x41));
	pci_write8(SB1(0x41), read|0xf0);
	// Lower thresholds (cause award does it)
	read=pci_read8(SB1(0x43));
	pci_write8(SB1(0x43), read|0x05);
	// PIO read prefetch counter (cause award does it)
	pci_write8(SB1(0x44), 0x18);
	// Use memory read multiple
	pci_write8(SB1(0x45), 0x1c);
	// address decoding.
	read=pci_read8(SB1(0x09));
	pci_write8(SB1(0x09), read & ~0x05);   /* both channels compat mode */
	// standard bios sets master bit.
	read=pci_read8(SB1(0x04));
	pci_write8(SB1(0x04), (read|0x07)&~0x80);

	// Use compatability mode - per award bios
	pci_write8(SB1(0x10), 0x00);
	pci_write8(SB1(0x14), 0x00);
	pci_write8(SB1(0x18), 0x00);
	pci_write8(SB1(0x1c), 0x00);

	// Force interrupts to use compat mode - just like Award bios
	pci_write8(SB1(0x3d), 0x00);
	pci_write8(SB1(0x3c), 0xff);
}

/* ---- VT8601 SDRAM command cycle: Rx6C[2:0] = cmd (other bits preserved) ---- */
#define CMD_NORMAL 0
#define CMD_NOP    1
#define CMD_PRE    2
#define CMD_MRS    3
#define CMD_CBR    4

static void sdram_cmd(uint8_t cmd)
{
	pci_write8(NB(0x6c), (pci_read8(NB(0x6c))&0xf8)|cmd);
	delay500();
}

/* Full SDRAM bring up of whichever row currently decodes address 0. */
static void sdram_init_row_at_0(void)
{
	sdram_cmd(CMD_NOP);
	mem_read32(0x00000000);
	delay500();

	sdram_cmd(CMD_PRE);
	mem_read32(0x00002000);
	delay500();   /* A10 high = precharge all */

	sdram_cmd(CMD_CBR);
	uint32_t i;
	for(i=0;i<8;i++)
	{
		mem_read32(0x00000000);
		delay500();
	}

	sdram_cmd(CMD_MRS);

	/* mode word = ((Rx64 & 0x20) >> 1 | 0x2a); read address = word << 3.   0xE4 -> 0x1D0 (CL3), CL2 -> 0x150 */
	uint32_t a=(((pci_read8(NB(0x64))& 0x20)>>1)|0x2a)<<3;
	mem_read32(a);
	delay500();
	sdram_cmd(CMD_NORMAL);

	sdram_cmd(CMD_NORMAL);
}

static void nb_init(void)
{
	/* rows 2..5 end at 128 MB */
	pci_write8(NB(0x5c), 0x10);
	pci_write8(NB(0x5d), 0x10);
	pci_write8(NB(0x5e), 0x10);
	pci_write8(NB(0x5f), 0x10);

	/* default register table (was a loop over 10 dword pairs at ROM 0xF1285) */
	pci_write32(NB(0x68), 0x2d867c51);
	pci_write32(NB(0x6c), 0x00003768);
	pci_write32(NB(0x64), 0x009494e4);
	pci_write32(NB(0x50), 0xd8c8dffe);
	pci_write32(NB(0xf8), 0x00004222);
	pci_write32(NB(0x58), 0x10080080);
	pci_write32(NB(0x54), 0x10100000);
	pci_write32(NB(0x70), 0x0cec88c0);
	pci_write32(NB(0x74), 0x0052810e);
	pci_write32(NB(0x78), 0x0001b401);

	/* SMBus host on (unused: SPD is never read), via VT8231 PM function */
	__builtin_outb(0x00, 0x5000);
	pci_write8 (SB4(0xd2), 0x01);
	pci_write32(SB4(0x90), 0x00005001);

	/* DRAM configuration, refresh off */
	pci_write8 (NB(0x60), 0x3f);
	pci_write8 (NB(0x69), 0x7c);
	pci_write8 (NB(0x64), 0xe4);
	pci_write8 (NB(0x65), 0x94);
	pci_write8 (NB(0x66), 0x94);
	pci_write8 (NB(0x67), 0x00);
	pci_write16(NB(0x58), 0x0080);
	pci_write8 (NB(0x5b), 0x08);          /* pass A: row 1 at 0..64MB, row 0 empty */
	pci_write8 (NB(0x5a), 0x00);
	pci_write8 (NB(0x68), 0x51);
	pci_write8 (NB(0x6a), 0x00);
	pci_write8 (NB(0x6b), 0x2d);
	pci_write8 (NB(0x6c), 0x68);
	pci_write8 (NB(0x6d), 0x21);
	pci_write8 (NB(0x61), 0xff);
	pci_write8 (NB(0x62), 0xff);
	pci_write16(NB(0x56), 0x1010);
	pci_write8 (NB(0x0d), 0x08);
	pci_write8 (NB(0x13), 0xe0);
	pci_write8 (NB(0xfd), 0x88);

	sdram_init_row_at_0();                /* pass A */

	pci_write8(NB(0x5b), 0x08);           /* pass B: row 0 at 0..64MB */
	pci_write8(NB(0x5a), 0x08);
	sdram_init_row_at_0();

	pci_write8(NB(0x5b), 0x10);           /* final map: row0 0..64MB, row1 64..128MB */
	pci_write8(NB(0x5a), 0x08);
	delay500();

	mem_write32(0x06000000, 0xff0055aa);    /* probe for row 1 at 96 MB */

	if(mem_read32(0x06000000)!=0xff0055aa)
	{
        pci_write8(NB(0x5b), 0x08);       /* only 64 MB fitted */
		uart_puts("Found 64MB\n");
	}
	else
		uart_puts("Found 128MB\n");

	delay500();
	pci_write8(NB(0x6a), 0x86);           /* refresh on */

	pci_write8(SB4(0xd2), 0x00);          /* SMBus host off */

	uint32_t i;
	for(i=0;i<0x400;i+=4)
		mem_write32(i, i);

	for(i=0;i<0x400;i+=4)
		mem_read32(i);
}

static uint32_t read_msr_lo(uint32_t index)
{
	uint32_t lo, hi;
	asm volatile ("rdmsr" : "=a" (lo), "=d" (hi) : "c" (index));
	return lo;
}

static void write_msr(unsigned long index, unsigned long lo, unsigned long hi)
{
	asm volatile ("wrmsr" : : "c" (index), "a" (lo), "d" (hi));
}

static uint32_t read_cr0(void)
{
	uint32_t cr0;
	asm volatile ("movl %%cr0, %0" : "=r" (cr0));
	return cr0;
}

static void write_cr0(uint32_t cr0)
{
	asm volatile ("movl %0, %%cr0" : : "r" (cr0));
}

static void wbinvd(void)
{
	asm volatile ("wbinvd");
}

static void early_cache_init(void)
{
	/* Caches are still off (CD|NW set in entry32).  Disable the MTRRs while reprogramming them. */
	wbinvd();

	uint32_t def_type=read_msr_lo(MSR_MTRR_DEF_TYPE);
	write_msr(MSR_MTRR_DEF_TYPE, def_type&~(MTRR_DEF_E|MTRR_DEF_FE), 0);

	/* Fixed ranges: write-through, except A0000-BFFFF (video) which stays uncacheable. */
	write_msr(MSR_MTRR_FIX64K_00000, MTRR_FIXED_ALL_WT, MTRR_FIXED_ALL_WT);
	write_msr(MSR_MTRR_FIX16K_80000, MTRR_FIXED_ALL_WT, MTRR_FIXED_ALL_WT);
	write_msr(MSR_MTRR_FIX16K_A0000, 0, 0);

	uint32_t i;
	for(i=MSR_MTRR_FIX4K_C0000;i<=MSR_MTRR_FIX4K_F8000;i++)
		write_msr(i, MTRR_FIXED_ALL_WT, MTRR_FIXED_ALL_WT);

	/* Clear all variable MTRRs. */
	for(i=MSR_MTRR_PHYS_BASE0;i<MSR_MTRR_PHYS_BASE0+2*MTRR_VAR_PAIRS;i++)
		write_msr(i, 0, 0);

	/* Total RAM in MB = northbridge Rx5B * 8. */
	uint32_t ram_mb=pci_read8(NB(0x5b))<<3;
	if(ram_mb==0)
	{
		uart_puts("No RAM reported by northbridge - halting\n");

		for (;;)
			__builtin_hlt();
	}

	/* MTRR0 covers [0, RAM) as write-back.  Assumes a power-of-two RAM size, as before. */
	write_msr(MSR_MTRR_PHYS_BASE0, MTRR_TYPE_WB, 0);
	write_msr(MSR_MTRR_PHYS_MASK0, ~((ram_mb<<20)-1)|MTRR_MASK_VALID, MTRR_PHYS_MASK_HI);

	/* Enable MTRRs. */
	def_type=read_msr_lo(MSR_MTRR_DEF_TYPE);
	write_msr(MSR_MTRR_DEF_TYPE, def_type|MTRR_DEF_E|MTRR_DEF_FE, 0);

	/* Caches on. */
	write_cr0(read_cr0()&~(CR0_CD|CR0_NW));
	wbinvd();
}

static void main(void)
{
    sb_prologue();
	delay_post();
	uart_init();

	uart_puts("\nFGT-60 early init\n");
    uart_puts("Stage 1: VT8231/UART up, VT8601 + SDRAM init...\n");

	nb_init();

	early_cache_init();
}
