
.code32

.section .text.entry
.global _start
.extern payload_main
.extern __stack_top

_start:
    cli
    cld

    # Establish our own stack
    movl $__stack_top, %esp
    andl $-16, %esp
    xorl %ebp, %ebp

    # Enter ordinary GCC-compiled C
    call payload_main

1:
    cli
    hlt
    jmp 1b
