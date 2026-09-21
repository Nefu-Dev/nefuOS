# nefuOS hard-disk boot sector (FAT32 reader, loaded via Windows BCD
# BOOTSECTOR entry at 0x7C00 with DL = boot disk).
# Finds first FAT32 partition, scans root dir first sector for KERNEL.BIN,
# reads the file cluster chain to 0x20000 via raw ATA PIO (no BIOS int13),
# then protected mode -> kernel. No Windows files modified.
# Memory: 0x7C00 boot, 0x7E00 vars, 0x7F00 BPB buf, 0x8100 FAT buf,
# 0x8300 dir buf, 0x20000 kernel.
.code16
.org 0
.section .text
.globl _start
.equ PART_LBA, 0x7E30
.equ RESV, 0x7E34
.equ SPC, 0x7E36
.equ FATREG, 0x7E38
.equ DATA_START, 0x7E3C
.equ CUR_CL, 0x7E40
.equ LOAD_SEG, 0x7E44
.equ ATA_LBA, 0x7E48
.equ SECBUF, 0x7F00
.equ FATBUF, 0x8100
.equ DIRBUF, 0x8300

_start:
    jmp main
    nop

# ---- protected mode entry (32-bit, at 0x7C04) ----
.org 0x04
.code32
pm_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movl $0x1F000, %esp
    ljmp $0x18, $0x20000

# ---- name pattern "KERNEL  BIN" (8.3) in the free slot 0x24-0x2F ----
.code16
.org 0x24
nk:
    .ascii "KERNEL  BIN"

# ---- GDT (0x30-0x58) ----
.org 0x30
gdtr:
    .word gdt_end - gdt - 1
    .long gdt + 0x7C00
gdt:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
    .quad 0x00CF9A000000FFFF
gdt_end:

# ---- main (0x58+) ----
.org 0x58
main:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw $0x7C00, %sp

    xorl %eax, %eax
    call rdsec              # read MBR
    movw $SECBUF + 0x1BE, %si
    movw $4, %cx
fp:
    cmpb $0, (%si)
    jne gp
    addw $16, %si
    loop fp
    jmp fail
gp:
    movl 8(%si), %eax
    movl %eax, PART_LBA
    call rdsec              # read BPB
    movzwl SECBUF + 0x0E, %eax
    movw %ax, RESV
    movl PART_LBA, %eax
    addl RESV, %eax
    movl %eax, FATREG       # FAT region start = part + resv
    movl SECBUF + 0x24, %ecx  # fat_sz
    shll $1, %ecx
    addl %ecx, %eax
    movl %eax, DATA_START   # data start = fatreg + 2*fat_sz
    movzwl SECBUF + 0x0D, %eax
    movw %ax, SPC

    # root cluster 2 -> DIRBUF (first sector); clba = data_start
    movw $DIRBUF, %di
    movl DATA_START, %eax
    movw $1, %cx
    call rdto
    movw $DIRBUF, %si
    movb $16, %cl
ds:
    cmpb $0, (%si)
    jz  fail
    cmpb $0xE5, (%si)
    jz  ne
    pushw %si
    movw $nk + 0x7C00, %di
    movw $11, %cx
    repe cmpsb
    popw %si
    jz  gf
ne:
    addw $32, %si
    loop ds
    jmp fail

gf:
    movzwl 0x1A(%si), %eax
    movl 0x14(%si), %ecx
    shll $16, %ecx
    orl  %ecx, %eax
    movl %eax, CUR_CL
    subw %bp, %bp
ll:
    movl CUR_CL, %eax
    call cfile
    movl CUR_CL, %eax
    call fnext
    testw %ax, %ax
    jz  ld
    movl %eax, CUR_CL
    movw SPC, %ax
    shlw $9, %ax
    addw %ax, %bp
    jmp ll
ld:
    cli
    lgdt gdtr + 0x7C00
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmp $0x08, $0x7C04

fail:
    jmp fail

# ---- EAX=lba -> SECBUF (es=0 from main) ----
rdsec:
    movw $1, %cx
    movw $SECBUF, %di
    jmp ata_read

# ---- EAX=lba, CX=sectors, ES:DI target ----
rdto:
    jmp ata_read

# ---- EAX=cluster -> read whole cluster to ES:BP (segment 0x2000) ----
cfile:
    movw $0x2000, %ax
    movw %ax, %es
    subw $2, %ax
    movzwl %ax, %eax
    movzwl SPC, %ecx
    mull %ecx
    addl DATA_START, %eax
    movw %bp, %di
    call ata_read           # cx = SPC sectors
    ret

# ---- FAT next: EAX=cluster -> EAX (0=end) ----
fnext:
    shll $2, %eax
    movl %eax, %edx
    movl %edx, %ecx
    shrl $9, %ecx
    addl FATREG, %ecx
    movw %dx, %si           # save byte offset low (si not used elsewhere)
    movl %ecx, %eax         # fat lba
    movw $1, %cx
    movw $FATBUF, %di
    xorw %ax, %ax
    movw %ax, %es
    call ata_read
    movw %si, %dx
    andw $0x1FF, %dx
    movzwl %dx, %edx
    movl FATBUF(%edx), %eax
    andl $0x0FFFFFFF, %eax
    cmpl $0x0FFFFFF8, %eax
    jae fend
    ret
fend:
    xorl %eax, %eax
    ret

# ---- raw ATA PIO read: EAX=lba, CX=sectors, ES:DI=dest ----
ata_read:
    pusha
    movw %cx, %bx           # bx = remaining sectors
    movl %eax, ATA_LBA
lsec:
    movw $0x1F2, %dx
    movb $1, %al
    outb %al, %dx           # sector count = 1
    movl ATA_LBA, %eax
    incw %dx
    outb %al, %dx           # lba0
    shrl $8, %eax
    incw %dx
    outb %al, %dx           # lba1
    shrl $8, %eax
    incw %dx
    outb %al, %dx           # lba2
    shrl $8, %eax
    andb $0x0F, %al
    orb  $0xE0, %al
    incw %dx
    outb %al, %dx           # drive select + lba3
    incw %dx
    movb $0x20, %al
    outb %al, %dx           # read sectors command
wbsy:
    inb %dx, %al
    testb $0x80, %al
    jnz wbsy                # wait BSY clear
    testb $0x08, %al
    jz wbsy                 # wait DRQ set
    testb $0x01, %al
    jnz fail                # ERR -> fail
    movw $0x1F0, %dx
    movw $256, %cx
    rep insw                # read 512B
    incl ATA_LBA
    decw %bx
    jnz lsec
    popa
    ret

.org 0x1FE
    .word 0xAA55
