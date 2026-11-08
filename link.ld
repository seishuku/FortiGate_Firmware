OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

/* ------------------------------------------------------------------------- */
/* Memory map                                                                */
/* ------------------------------------------------------------------------- */

/* Low RAM reserved for BSS and stack. */
__reserved_lo = 0x90000;
__stack_top   = 0xA0000;

/* Full 256 KiB flash mapping, top of the 32-bit address space. */
__rom_base = 0xFFFC0000;
__rom_top  = 0x100000000;

/* Stage 1 runs from the legacy F segment (RAM shadow) and is stored in the
 * last 64 KiB of flash.  start.S copies LMA -> VMA at the end of the tail. */
__shadow_vma   = 0x000F0000;
__shadow_lma   = 0xFFFF0000;
__shadow_size  = 0x00010000;
__shadow_delta = __shadow_lma - __shadow_vma;

/* Stage 2 executes from low RAM (below 1 MiB, clear of the FILO payload which links at 0x100000)
 * and is stored at the start of flash. */
__stage2_vma   = 0x00010000;

SECTIONS
{
    /*
     * Stage 1: executes at 0xF0000, stored in the last 64 KiB of flash.
     * .rom_text and .rom_data follow .init; their LMAs track their VMAs
     * automatically (same VMA-LMA offset as the preceding section).
     */
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

    /* The last 16 bytes of the shadow window hold the reset vector. */
    ASSERT(. <= __shadow_vma + __shadow_size - 16,
           "stage 1 does not fit in the 64 KB shadow window (reset vector overlap)")

    /*
     * Stage 2: linked to execute from RAM at 1 MiB, stored at the start of
     * flash.  Copied there by stage2_entry.  Size is a multiple of 4 so the
     * copy can use dword moves.
     */
    .stage2 __stage2_vma : AT(__rom_base)
    {
        __stage2_start = .;

        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.data .data.*)
        *(.sdata .sdata.*)

        . = ALIGN(4);
        __stage2_end = .;
    }

    __stage2_load     = LOADADDR(.stage2);
    __stage2_size     = SIZEOF(.stage2);
    __stage2_load_end = LOADADDR(.stage2) + SIZEOF(.stage2);

    ASSERT(__stage2_load_end <= __shadow_lma,
           "stage 2 overlaps the first-stage ROM region")

    ASSERT(__stage2_end <= __reserved_lo,
           "stage 2 runs into the .bss/stack region")

    /*
     * Reset vector: the CPU starts at the top of the 32-bit address space.
     */
    .reset (__rom_top - 16) : AT(__rom_top - 16)
    {
        *(.reset)
    }

    ASSERT(SIZEOF(.reset) <= 16,
           "reset vector section is larger than 16 bytes")

    /*
     * BSS lives in reserved low RAM and is zeroed by stage2_entry.
     */
    .bss __reserved_lo (NOLOAD) :
    {
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    }

    ASSERT(__bss_end <= __reserved_lo + 0x4000,
           ".bss is larger than the 16 KB reserved region")

    ASSERT(__bss_end < __stack_top,
           ".bss runs into the stack")

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
