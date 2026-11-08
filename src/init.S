/*
 * FGT-60 standalone ROM startup code.
 *
 *   _start          real-mode entry (the reset vector jumps to F000:0000): load the GDT, enter protected mode
 *   entry32         flat segments, caches off, run romcc stage 1
 *   stage1          romcc output (stage1.inc): RAM init, early cache init.
 *                   Falls through into stage 2.
 *   stage2_entry    copy stage 2 from flash to RAM, set up stack + .bss, call stage2_main()
 *   reset vector    last 16 bytes of the ROM: jmp F000:0000
 *
 * Stage 1 executes in place from flash through the legacy F-segment alias (0xF0000); it is not shadowed
 * into RAM.  If something downstream needs the F segment in RAM, copy it from stage 2 (C), not here.
 *
 * Section layout (see link.ld):
 *   .init      real-mode stub, GDT, 32-bit entry (must start at linear 0xF0000)
 *   .rom.text  puts, stage 1 (romcc), tail, stage 2 entry
 *   .reset     reset vector (linear 0xFFFF0)
 */
	.arch i686
	.intel_syntax noprefix

/* Declare the sections up front with explicit flags. Do not remove these. */
	.section .rom.data, "a"
	.section .rom.text, "ax"
	.section .reset, "ax"
	.section .init, "ax"

/* ------------------------------------------------------------------------- */
/* Constants                                                                 */
/* ------------------------------------------------------------------------- */

/* GDT selectors */
	.equ SEL_CODE32,        0x08
	.equ SEL_DATA32,        0x10

/* GDT descriptor access bytes */
	.equ GDT_ACCESS_CODE,   0x9b            /* present, ring 0, code, readable   */
	.equ GDT_ACCESS_DATA,   0x93            /* present, ring 0, data, writable   */

/* CR0 bits */
	.equ CR0_PE,            (1 << 0)
	.equ CR0_MP,            (1 << 1)
	.equ CR0_EM,            (1 << 2)
	.equ CR0_TS,            (1 << 3)
	.equ CR0_NE,            (1 << 5)
	.equ CR0_WP,            (1 << 16)
	.equ CR0_AM,            (1 << 18)
	.equ CR0_NW,            (1 << 29)
	.equ CR0_CD,            (1 << 30)
	.equ CR0_PG,            (1 << 31)

	.equ CR0_CLEAR_MASK,    (~(CR0_PG | CR0_CD | CR0_NW | CR0_AM | CR0_WP | CR0_NE | CR0_TS | CR0_EM | CR0_MP)) & 0xffffffff

/* 16550 UART (COM1) */
	.equ COM1_THR,          0x3f8
	.equ COM1_LSR,          0x3fd
	.equ LSR_THRE,          0x20

/* ------------------------------------------------------------------------- */
/* Macros                                                                    */
/* ------------------------------------------------------------------------- */

/* Flat 4 GB segment descriptor (base 0, limit 0xfffff, 4K granularity, 32-bit). */
	.macro GDT_FLAT_SEGMENT access
	.word 0xffff                            /* limit 15:0 */
	.word 0x0000                            /* base 15:0 */
	.byte 0x00                              /* base 23:16 */
	.byte \access
	.byte 0xcf                              /* G=1, D=1, limit 19:16 = 0xf */
	.byte 0x00                              /* base 31:24 */
	.endm

/* ------------------------------------------------------------------------- */
/* Real-mode entry                                                           */
/* ------------------------------------------------------------------------- */
	.section .init, "ax"
	.globl _start
	.code16
_start:
	cli
	xor  eax, eax
	mov  cr3, eax                           /* known paging state */
	wbinvd

	/* CS base is 0xF0000 after the reset vector's far jump, so address the GDTR through CS. */
	data32 lgdt cs:[gdtr - _start]

	mov  eax, cr0
	and  eax, CR0_CLEAR_MASK
	or   eax, (CR0_CD | CR0_PE)             /* protected mode, caches disabled */
	mov  cr0, eax

	data32 jmp SEL_CODE32:entry32           /* far jump, 32-bit offset */

/* ------------------------------------------------------------------------- */
/* GDT                                                                       */
/* ------------------------------------------------------------------------- */
	.balign 8
gdtr:
	.word gdt_end - gdt - 1
	.long gdt

	.balign 8
gdt:
	.quad 0x0000000000000000                /* 0x00: null */
	GDT_FLAT_SEGMENT GDT_ACCESS_CODE        /* 0x08: SEL_CODE32 */
	GDT_FLAT_SEGMENT GDT_ACCESS_DATA        /* 0x10: SEL_DATA32 */
gdt_end:

/* ------------------------------------------------------------------------- */
/* 32-bit entry                                                              */
/* ------------------------------------------------------------------------- */
	.code32
	.balign 16
entry32:
	mov  ax, SEL_DATA32
	mov  ds, ax
	mov  es, ax
	mov  ss, ax
	mov  fs, ax
	mov  gs, ax

	invd                                    /* discard anything left in the caches */
	mov  eax, cr0
	or   eax, (CR0_CD | CR0_NW)             /* caches stay off until stage 1 enables them */
	mov  cr0, eax

	jmp  stage1

/* ------------------------------------------------------------------------- */
/* romcc stage 1 (falls through into stage2)                                   */
/* ------------------------------------------------------------------------- */
stage1:
	.att_syntax prefix
	.include "stage1.inc"
	.intel_syntax noprefix
	.code32

/* ------------------------------------------------------------------------- */
/* Stage 2 entry                                                             */
/* Flat segments are loaded, caches are on, RAM works.  Copy stage 2 into    */
/* place, set up a stack and .bss, then enter C.  Memory map is in link.ld.  */
/* ------------------------------------------------------------------------- */
	.section .rom.text, "ax"                /* stage1.inc may leave us in another section; tail must follow it directly */
	.extern stage2_main
stage2_entry:
	cli
	cld

	/* Copy stage 2 from flash to its RAM execution address. */
	mov  esi, offset __stage2_load
	mov  edi, offset __stage2_start
	mov  ecx, offset __stage2_size          /* multiple of 4: link.ld does ALIGN(4) inside .stage2 */
	shr  ecx, 2
	rep  movsd

	/* Stack. */
	mov  esp, offset __stack_top
	xor  ebp, ebp

	/* Zero .bss. */
	mov  edi, offset __bss_start
	mov  ecx, offset __bss_end
	sub  ecx, edi
	shr  ecx, 2
	xor  eax, eax
	rep  stosd

	call stage2_main

stage2_hang:
	hlt
	jmp  stage2_hang

/* ------------------------------------------------------------------------- */
/* Reset vector (linear 0xFFFF0)                                             */
/* ------------------------------------------------------------------------- */
	.section .reset, "ax"
	.code16
reset_vector:
	jmp  0xf000:0x0000                      /* -> _start */
	.balign 16, 0xff
