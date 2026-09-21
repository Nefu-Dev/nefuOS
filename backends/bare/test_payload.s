# Test payload for boot_hd verification: prints 'P' to debugcon, then halts.
.code32
.org 0
.section .text
.globl _start
_start:
    movw $0xE9, %dx
    movb $'P', %al
    outb %al, %dx
1:
    jmp 1b

.org 0xBB8
    .fill 1, 1, 0
