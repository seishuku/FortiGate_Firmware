
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

PHDRS
{
    image PT_LOAD FLAGS(7);
}

SECTIONS
{
    . = 0x00200000;

    .text : {
        KEEP(*(.text.entry))
        *(.text*)
    } :image

    .rodata : {
        *(.rodata*)
    } :image

    .data : {
        *(.data*)
    } :image

    .bss : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)

        /* Reserve a private 16 KB stack */
        . = ALIGN(16);
        . += 16384;
        __stack_top = .;

        __bss_end = .;
    } :image

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
