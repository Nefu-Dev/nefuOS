# 二分测试：最小引导 + 主引导同款除法 CHS 计算
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
    movb %dl, boot_drive
    call dbg
    .byte 'A'
    movw $0x4F02, %ax
    movw $0x4115, %bx
    int $0x10
    cmpw $0x004F, %ax
    jne .nofail
    call dbg
    .byte 'V'
.nofail:
    movw $0, load_count
.loop:
    movw load_count, %ax
    cmpw $1, %ax             # kernel_sectors = 1
    jae .done
    xorl %edx, %edx
    movw $18, %cx
    divw %cx
    movw %dx, tmp_sec
    movw %ax, %cx
    xorl %edx, %edx
    movw %cx, %ax
    movw $2, %cx
    divw %cx
    movw %ax, tmp_cyl
    movw %dx, tmp_head
    movw $18, %ax
    subw tmp_sec, %ax
    cmpw $16, %ax
    jbe .nsec_ok
    movw $16, %ax
.nsec_ok:
    movw %ax, tmp_nsec
    movb $0x02, %ah
    movb tmp_nsec, %al
    movb tmp_cyl, %ch
    movb tmp_sec, %cl
    incb %cl
    movb tmp_head, %dh
    movb boot_drive, %dl
    movzwl load_count, %eax
    shll $5, %eax
    addl $0x2000, %eax
    movw %ax, %es
    xorw %bx, %bx
    int $0x13
    jnc .ok
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
    .byte 'D'
    movw tmp_nsec, %ax
    addw %ax, load_count
    jmp .loop
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

boot_drive: .byte 0
load_count: .word 0
tmp_sec: .word 0
tmp_head: .word 0
tmp_cyl: .word 0
tmp_nsec: .word 0
.org 508
.word 0xAA55
