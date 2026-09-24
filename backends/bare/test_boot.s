# minimal test boot sector: VBE -> read 1 sector -> print result
.code16
.org 0
.section .text
.globl _start
_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp
    sti
    movb %dl, (0x7E00)
    # probe 1: 'A' = started
    call dbg
    .byte 'A'
    # VBE 800x600x32
    movw $0x4F02, %ax
    movw $0x4115, %bx
    int $0x10
    cmpw $0x004F, %ax
    jne .nofail
    call dbg
    .byte 'V'
    # read the 0x4F01 mode-info block back to 0x6000 (same as the main bootloader)
    movw $0x4F01, %ax
    movw $0x4115, %cx
    xorw %di, %di
    movw %di, %es
    movw $0x6000, %di
    int $0x10
    xorw %ax, %ax
    movw %ax, %es
    call dbg
    .byte 'M'
.nofail:
    # cli control test
    cli
    # read 1: sectors 1-16 (CHS 0,0,1) to 0x20000; store the count in 0x7E02
    movw $0, (0x7E02)
    movb $0x02, %ah
    movb $16, %al
    xorw %cx, %cx
    movb $1, %cl
    xorw %dx, %dx
    movb (0x7E00), %dl
    movzwl (0x7E02), %edi
    shll $5, %edi
    addl $0x2000, %edi
    movw %di, %es
    xorw %bx, %bx
    int $0x13
    jnc .ok1
    call dbg
    .byte 'F'
    jmp .done
.ok1:
    call dbg
    .byte 'D'
    # read 2: sectors 17-18 (CHS 0,0,17) to 0x22000; add 16 to count first
    movw $16, (0x7E02)
    movb $0x02, %ah
    movb $2, %al
    xorw %cx, %cx
    movb $17, %cl
    xorw %dx, %dx
    movb (0x7E00), %dl
    movzwl (0x7E02), %edi
    shll $5, %edi
    addl $0x2000, %edi
    movw %di, %es
    xorw %bx, %bx
    int $0x13
    jnc .ok
    # print the high nibble of AH
    pushw %ax
    movb %ah, %al
    shrb $4, %al
    andb $0x0F, %al
    addb $'0', %al
    cmpw $0x3A, %ax
    jb .n1
    addb $7, %al
.n1:
    movw $0xE9, %dx
    outb %al, %dx
    popw %ax
    call dbg
    .byte 'F'
    jmp .done
.ok:
    call dbg
    .byte 'O'
.done:
    call dbg
    .byte 'X'
.halt:
    hlt
    jmp .halt

dbg:
    popw %si
    lodsb
    movw $0xE9, %dx
    outb %al, %dx
    pushw %si
    ret

.org 510
.word 0xAA55
