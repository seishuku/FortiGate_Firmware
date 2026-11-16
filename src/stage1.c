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
    /* Disable UART interrupts while firmware uses polling. */
    __builtin_outb(0x00, 0x3f9);

    /* Set divisor = 1 (115200 baud with 1.8432 MHz clock). */
    __builtin_outb(0x80, 0x3fb);  /* DLAB = 1 */
    __builtin_outb(0x01, 0x3f8);  /* Divisor low */
    __builtin_outb(0x00, 0x3f9);  /* Divisor high */

    /* 8 data bits, no parity, 1 stop bit; DLAB = 0. */
    __builtin_outb(0x03, 0x3fb);

    /* DTR and RTS asserted. */
    __builtin_outb(0x03, 0x3fc);

    /* Enable FIFO and clear both FIFOs. */
    __builtin_outb(0x07, 0x3fa);
}

static void sio_write(uint8_t idx, uint8_t val)
{
	__builtin_outb(idx, 0x3f0);
	__builtin_outb(val, 0x3f1);
}

static void sb_prologue(void)
{
	pci_write8(SB0(0x43), 0xe0);
	pci_write8(SB0(0x50), 0xf6);

	/* Enable COM1, disable parallel port, leave FDC disabled. */
	sio_write(0xf2, 0x07);
	sio_write(0xf3, 0x00);
	sio_write(0xf4, 0xfe);

	pci_write8(SB0(0x50), 0xf2);

	// USB (this device only has 2 ports on one controller)
	pci_write8(SB0(0x50), pci_read8(SB0(0x50))&~(0x10));
	// pci_write8(SB0(0x50), pci_read8(SB0(0x50))&~(0x20));

	// COM1 / Serial Port A IRQ routing.
    pci_write8(SB0(0x46), (pci_read8(SB0(0x46))&0xf0)|0x04);

	// Enable ISA IRQ assertion through SERIRQ.
    pci_write8(SB0(0x52), (pci_read8(SB0(0x52))&~0x04)|0x08);

	pci_write8 (SB0(0x6c), pci_read8(SB0(0x6c))|0x80); // internal IO decode

	pci_write8 (SB0(0x40), pci_read8(SB0(0x40))|0x44); // IO recovery time
	pci_write8 (SB0(0x42), pci_read8(SB0(0x42))|0xf0); // something award does?
	pci_write8 (SB0(0x47), pci_read8(SB0(0x47))|0x20); // enable legacy PIC ports
	pci_write8 (SB0(0x4a), pci_read8(SB0(0x4a))|0x08); // dummy PCI request
	pci_write8 (SB0(0x4c), (pci_read8(SB0(0x4c))&~0x0f)|0x04); // set default IDE interrupt values
	pci_write8 (SB0(0x4f), pci_read8(SB0(0x4f))|0x08); // INIT# as CPU reset
	pci_write8 (SB0(0x51), pci_read8(SB0(0x51))|0x18);
	pci_write32(SB0(0x54), 0xc0a9b000); // PCI IRQ level select and PCI IRQ[a,b,c,d] routing
	pci_write32(SB0(0x58), 0x00000003);
	pci_write32(SB0(0x5c), 0x40f00000);
	pci_write32(SB0(0x60), 0x00000000);
	pci_write32(SB0(0x64), 0x08020000);
	pci_write32(SB0(0x68), 0x00000001);

	// IDE enable
	pci_write8(SB1(0x40), pci_read8(SB1(0x40))|0x03);
	// Enable prefetch buffers
	pci_write8(SB1(0x41), pci_read8(SB1(0x41))|0xf0);
	// Lower thresholds (cause award does it)
	pci_write8(SB1(0x43), pci_read8(SB1(0x43))|0x05);
	// PIO read prefetch counter (cause award does it)
	pci_write8(SB1(0x44), 0x18);
	// Use memory read multiple
	pci_write8(SB1(0x45), 0x1c);
	// address decoding.
	pci_write8(SB1(0x09), pci_read8(SB1(0x09))&~0x05);   /* both channels compat mode */
	// standard bios sets master bit.
	pci_write8(SB1(0x04), (pci_read8(SB1(0x04))|0x07)&~0x80);

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

	pci_write8(NB(0x6a), 0x86);           /* refresh on */

	pci_write8(SB4(0xd2), 0x00);          /* SMBus host off */

	uint32_t i;
	for(i=0;i<0x400;i+=4)
		mem_write32(i, i);

	for(i=0;i<0x400;i+=4)
		mem_read32(i);
}

// Do PCI bus initialization
static void trident_pci_init(void)
{
    // Enable integrated VGA and allocate 4MB UMA frame buffer space in Northbridge (Dev 0:0.0, reg 0xFB)
    pci_write8(NB(0xfb), 0xa0);

    // Set primary bus = 0, secondary bus = 1, subordinate bus = 1
    pci_write32(PCI_DEV(0, 1, 0, 0x18), 0x00010100);
    
    // Enable VGA forwarding in bridge control register
    pci_write16(PCI_DEV(0, 1, 0, 0x3E), pci_read16(PCI_DEV(0, 1, 0, 0x3E)) | 0x0008);

    // Configure non-prefetchable memory window covering the graphics BAR range
    pci_write16(PCI_DEV(0, 1, 0, 0x20), 0xFFF0);
    pci_write16(PCI_DEV(0, 1, 0, 0x22), 0x0000);
    pci_write16(PCI_DEV(0, 1, 0, 0x20), 0xE500); // Window base: 0xE5000000
    pci_write16(PCI_DEV(0, 1, 0, 0x22), 0xE67F); // Window limit: 0xE67FFFFF
    
    // Ensure bridge forwards memory transactions
    pci_write16(PCI_DEV(0, 1, 0, 0x04), pci_read16(PCI_DEV(0, 1, 0, 0x04)) | 0x0002);

    // Keep decoding disabled while programming BARs
    pci_write16(PCI_DEV(1, 0, 0, 0x04), 0x0000);

    pci_write32(PCI_DEV(1, 0, 0, 0x10), 0xE5000000); // BAR0: 8MB Frame Buffer Aperture
    pci_write32(PCI_DEV(1, 0, 0, 0x14), 0xE6000000); // BAR1: 128KB MMIO Registers
    pci_write32(PCI_DEV(1, 0, 0, 0x18), 0xE5800000); // BAR2: Secondary Aperture

    // Enable I/O space (bit 0) and Memory space (bit 1) so port 0x3C4/0x3C5 are active.
    // (Leave bus mastering disabled for now or enable it via 0x0007 if needed).
    pci_write16(PCI_DEV(1, 0, 0, 0x04), 0x0003);

    // Enable Bus Master (bit 2) alongside I/O and Memory decoding for the Linux driver (0x0007)
    pci_write16(PCI_DEV(1, 0, 0, 0x04), 0x0007);

    // Clear any stale PCI status error flags on both devices
    pci_write16(PCI_DEV(1, 0, 0, 0x06), 0xFFFF);
    pci_write16(PCI_DEV(0, 1, 0, 0x06), 0xFFFF);
}

// Sequencer register read/write helpers
static inline void sr_w(uint8_t i, uint8_t v)
{
	outb(i, 0x3C4);
	outb(v, 0x3C5);
}

static inline uint8_t sr_r(uint8_t i)
{
	outb(i, 0x3C4);
	return inb(0x3C5);
}

// CRT controller register read/write
static inline uint8_t cr_r(uint8_t i)
{
	outb(i, 0x3D4);
	return inb(0x3D5);
}

static inline void cr_w(uint8_t i, uint8_t v)
{
	outb(i, 0x3D4);
	outb(v, 0x3D5);
}

// Graphics controller register write
static inline void gr_w(uint8_t i, uint8_t v)
{
	outb(i, 0x3CE);
	outb(v, 0x3CF);
}

// ROM 0x260f: CR1F low nibble from UMA size field, CR1A/1B/1C from NB config
static void memory_interface_init(void)
{
	uint8_t uma=pci_read8(NB(0xFB))&0x30;
	uint8_t code=0x7;

	if(uma==0x20)
		code=0xF;

	if(uma==0x30)
		code=0x4;

	cr_w(0x1F, (cr_r(0x1F)&0xF0)|code);

	cr_w(0x1A, 0x07);
	cr_w(0x1B, 0x07);
	cr_w(0x1C, 0x07);
}

// Do VGA device initialization
static void trident_vga_init(void)
{
	// ROM 0x5876: legacy VGA enable
	outb(0x01, 0x3C3); // VGA video subsystem enable
	outb(0x08, 0x46E8); // VGA display adapter enable
	outb(0x67, 0x3C2);  // Misc outputs, sets color, IO, RAM enable, 25MHz?

	sr_w(0x11, 0x92); // Unlock extended regs

	// Undocumented, no idea.
	// It's needed.
	cr_w(0xC2, 0x00);
	cr_w(0xC0, 0x00);
	cr_w(0xC1, 0x41);
	cr_w(0xCF, 0x03);

	// Set up sequencer block
	sr_w(0x0B, 0xF3); // "old" register set mode (for SRD, SRE, and GRE registers)
	sr_w(0x0D, 0x08); // Set CPU bandwidth select to non-interrupted access

	(void)sr_r(0x0B); // "new" register set mode (for SRD, SRE, and GRE registers)
	sr_w(0x0D, 0x00); // Set video clock divide by 1, FIFO empty level 0

	sr_w(0x1A, 0xFF); // VCLK2 generator numerator - ??
	sr_w(0x1B, 0xFF); // VCLK2 generator K-Factor and denuminator - ??
	sr_w(0x20, 0x04); // Clock synth and RAMDAC setup - Video mode enable
	sr_w(0x24, 0x4F); // Power management control - DAC on/Disable MCLK VCO/Slow MCLK/1 clock input divisor/Disable VCLK2 VCO
	sr_w(0x37, 0x30); // Video key mode - Feature Connector Input Pixel Clock Tuning@12ns
	sr_w(0x38, 0x10); // Advanced feature connector control - Invert DCLK output polarity
	sr_w(0xD9, 0x08); // Digital TV interface control - Set VGAHS, VGAVS, and PCLK x 2

	// Set up memory interface (depends on chipset/UMA)
	memory_interface_init();

	// CRTC extended regs
	cr_w(0x3A, 0x30); // Physical address control - Both IO/Memory address linearization
	cr_w(0x39, 0x05); // PCI interface control - VGA IO space can be memory mapped/PCI burst read
	cr_w(0x20, 0x20); // Command FIFO - Enable write buffer
	cr_w(0x25, 0x87); // RAMDAC read/write timing - Set wait states/Pixel clock tristate control disable
	cr_w(0x29, 0x04); // RAMDAC mode - Internal RAMDAC
	cr_w(0x2D, 0x92); // GE timing control - Buffer read delay cycles/Sample clock delay
	cr_w(0x2F, 0xFF); // Performance tuning - VCLK1/Normal read cycle/8 FIFO depth/2 refresh cycles per line
	cr_w(0x30, 0x0F); // Something with FIFO latency control?
	cr_w(0x36, 0x00); // Graphics/Video engine control
	cr_w(0x38, 0x10); // Pixel bus mode - Standard VGA 64bit config
	cr_w(0x3B, 0x31); // Clock and tuning - Vert retrace memory refresh enable/??

	// Power management
	gr_w(0x20, 0x80); // Standby timer control
	gr_w(0x21, 0xC1); // Power management control 1
	gr_w(0x22, 0x12); // Power management control 2
	gr_w(0x23, 0x08); // Power status
	gr_w(0x24, 0xE0); // Soft power control
	gr_w(0x25, 0x00); // Power control select
	gr_w(0x26, 0x08); // DPMS control
	gr_w(0x2A, 0x81); // Suspend pin timer
	gr_w(0x2C, 0x04); // Misc pin control
	gr_w(0x2F, 0x23); // Misc internal control

	sr_w(0x11, 0x87); // Relock extended regs
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

uint32_t test_ram(void)
{
    uint32_t count, val;
    volatile uint32_t *p;

    /* Start probing right from 0 MB */
    count=0;
    
    while(count<1024)
	{
        p=(volatile uint32_t *)(count<<20);
        
        /* Save original content */
        val=*p;
        
        /* Test Pattern 1 */
        *p=0x55aa55aa;
        if(*p!=0x55aa55aa)
		{
            *p=val;
            break;
        }
        
        /* Test Pattern 2 */
        *p=0xaa55aa55;
        if(*p!=0xaa55aa55)
		{
            *p=val;
            break;
        }
        
        /* Restore memory and move to next MB */
        *p=val;
        count++;

		uart_putc('.');
    }
    
	uart_puts("\nCounted RAM: ");
	uart_uint32(count);
	uart_puts("MB\n");

    return count;
}

static uint32_t early_cache_init(void)
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

	/* Test RAM to get working count */
	uint32_t ram_mb=test_ram();

	/* MTRR0 covers [0, RAM) as write-back.  Assumes a power-of-two RAM size, as before. */
	write_msr(MSR_MTRR_PHYS_BASE0, MTRR_TYPE_WB, 0);
	write_msr(MSR_MTRR_PHYS_MASK0, ~((ram_mb<<20)-1)|MTRR_MASK_VALID, MTRR_PHYS_MASK_HI);

	/* Enable MTRRs. */
	def_type=read_msr_lo(MSR_MTRR_DEF_TYPE);
	write_msr(MSR_MTRR_DEF_TYPE, def_type|MTRR_DEF_E|MTRR_DEF_FE, 0);

	/* Caches on. */
	write_cr0(read_cr0()&~(CR0_CD|CR0_NW));
	wbinvd();

	return ram_mb<<20;
}

static uint32_t main(void)
{
    sb_prologue();
	delay_post();
	uart_init();

	uart_puts("\nFGT-60 early init\n");
    uart_puts("Stage 1:\n\n");

	uart_puts("VT8601 setup + SDRAM...\n");
	nb_init();

	uart_puts("Trident PCI configuration...\n");
	trident_pci_init();
	uart_puts("Trident VGA configuration...\n");
	trident_vga_init();

	uart_puts("Enabling cache and testing RAM...\n");
	return early_cache_init();
}
