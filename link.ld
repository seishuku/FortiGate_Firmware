OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

/*
 * Low-memory layout
 *
 * Stage 2 executes from 0x10000.
 *
 * The bootloader heap occupies the unused space between the end of Stage 2 and the beginning of BSS.
 *
 * BSS is located at 0x90000.
 * Stack occupies the top 16 KiB below 0xA0000.
 *
 * NOTE:
 * The EBDA location should be checked at runtime before relying on the entire low-memory region.
 */

__reserved_lo = 0x00090000;

__stack_top    = 0x000A0000;
__stack_size   = 0x00004000;
__stack_bottom = __stack_top - __stack_size;

/*
 * ROM layout
 *
 * Full 256 KiB flash mapping:
 *   0xFFFC0000 - 0xFFFFFFFF
 *
 * Stage 2 is stored at the beginning of flash.
 * Stage 1 occupies the final 64 KiB.
 */

__rom_base = 0xFFFC0000;
__rom_top  = 0x100000000;

/*
 * Stage 1 runs from the legacy F-segment shadow RAM.
 *
 * VMA: 0x000F0000
 * LMA: 0xFFFF0000
 * Size: 64 KiB maximum
 *
 * init.S copies the image from its flash LMA to its
 * shadow VMA.
 */

__shadow_vma   = 0x000F0000;
__shadow_lma   = 0xFFFF0000;
__shadow_size  = 0x00010000;
__shadow_delta = __shadow_lma - __shadow_vma;

__linux_sysseg_start = 0x00010000;
__linux_sysseg_end   = 0x00012000;

/*
 * Stage 2 executes from low RAM.
 *
 * VMA: 0x00010000
 * LMA: 0xFFFC0000
 *
 * Stage 2 must remain below 0x90000.
 */

__stage2_vma = 0x00020000;

SECTIONS
{
	/* Stage 1 */
	.init __shadow_vma : AT(__shadow_lma)
	{
		*(.init)
	}

	.rom_text :
	{
		*(.rom.text)
	}

	.rom_data :
	{
		*(.rom.data)
	}

	ASSERT(. <= __shadow_vma + __shadow_size - 16,
		   "Stage 1 does not fit in the 64 KB shadow window (reset vector overlap)")

	/* Stage 2 */
	.stage2 __stage2_vma : AT(__rom_base)
	{
		__stage2_start = .;

		*(.text .text.*)
		*(.rodata .rodata.*)
		*(.data .data.*)
		*(.sdata .sdata.*)

		. = ALIGN(16);

		__stage2_end = .;
	}

	__stage2_load     = LOADADDR(.stage2);
	__stage2_size     = SIZEOF(.stage2);
	__stage2_load_end = LOADADDR(.stage2) + SIZEOF(.stage2);


	/*
	 * Heap is dynamically sized according to the final linked Stage 2 image size.
	 *
	 * Start: aligned end of Stage 2
	 * End:   beginning of BSS
	 *
	 * This is a low-memory heap for bootloader allocations.
	 */
	__stage2_heap_start = ALIGN(__stage2_end, 16);
	__stage2_heap_end   = __reserved_lo;
	__stage2_heap_size  = __stage2_heap_end - __stage2_heap_start;


	/* BSS */
	.bss __reserved_lo (NOLOAD) :
	{
		__bss_start = .;

		*(.bss .bss.*)
		*(.sbss .sbss.*)
		*(COMMON)

		. = ALIGN(16);

		__bss_end = .;
	}


	/*
	 * Stack is reserved above BSS.
	 * The stack grows downward.
	 */
	__stack_region_start = __stack_bottom;
	__stack_region_end   = __stack_top;

	/* Reset vector */
	.reset (__rom_top - 16) : AT(__rom_top - 16)
	{
		*(.reset)
	}


	/* Discard unwanted sections */
	/DISCARD/ :
	{
		*(.comment)
		*(.note*)
		*(.eh_frame*)
	}
}

/* Link-time assertions */

ASSERT(__stage2_end <= __reserved_lo,
       "Stage 2 runs into the BSS/heap region")

/* Stage 2 must not overlap Stage 1 in ROM. */
ASSERT(__stage2_load_end <= __shadow_lma,
	   "Stage 2 overlaps the first-stage ROM region")

/* Stage 2 must fit below the BSS reservation. */
ASSERT(__stage2_end <= __reserved_lo,
	   "Stage 2 runs into the BSS/heap region")

/* Heap must have usable space. */
ASSERT(__stage2_heap_start < __stage2_heap_end,
	   "Bootloader heap has no available space")

ASSERT(__stage2_heap_size >= 0x1000,
	   "Bootloader heap is smaller than 4 KB")

/* BSS must fit below the stack. */
ASSERT(__bss_end <= __stack_bottom,
	   "BSS overlaps the stack")

/* Stack must have a valid size. */
ASSERT(__stack_bottom < __stack_top,
	   "Invalid stack boundaries")

/* Stack must remain below the conventional-memory boundary. */
ASSERT(__stack_top <= 0x000A0000,
	   "Stack extends into VGA/BIOS memory")

/* BSS size limit retained from the original script. */
ASSERT(__bss_end <= __reserved_lo + 0x4000,
	   "BSS is larger than the 16 KB reserved region")

/* Reset vector must fit in its 16-byte region. */
ASSERT(SIZEOF(.reset) <= 16,
	   "Reset vector section is larger than 16 bytes")
