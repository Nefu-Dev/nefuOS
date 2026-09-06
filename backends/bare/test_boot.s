# 最小测试引导扇区：VBE -> 读1扇区 -> 打印结果
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
    # 探针1: 'A' = 已启动
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
    # 0x4F01 模式信息回读到 0x6000（主引导同款）
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
    # cli 对照测试
    cli
    # 读1：扇区 1-16（CHS 0,0,1）到 0x20000；用 0x7E02 存 count
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
    # 读2：扇区 17-18（CHS 0,0,17）到 0x22000；count 先加 16
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
    # 打印 AH 高四位
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
