# nefuOS bare boot sector (El Torito no-emulation, DL=0xE0 real ATAPI CD)
# Duties: VBE 800x600x32 -> store LFB/bootinfo at 0x7000 -> load kernel.bin
# from CD LBA24 (57 x 2048B sectors) to 0x20000 -> protected mode -> long
# mode entry (entry.s) -> nefuos_kernel_main.
# Key design:
#  1) SeaBIOS no-emulation: DL=0xE0, physical 2048B sectors. int13 AH=0x42
#     is served by the real ATAPI stack, single call <= 32 sectors (64KB).
#  2) kernel.bin loaded via 4 static chunks: 32+32+32+1 (dap1 -> 0x20000,
#     dap2 -> 0x30000, dap3 -> 0x40000, dap4 -> 0x50000, tail). DAPs are
#     static (SeaBIOS clobbers BP/SI/ES on int13) and live in the safe zone
#     below 0x7DAA.
#  3) After each chunk, verify the first byte of the target; a CF=0 result
#     with no data written (ATAPI silent failure) is retried per-chunk.
#  4) Boot sector tail 0x7DAA-0x7DFF gets clobbered by BIOS int 0x10/0x13,
#     so GDT/kernel_count/DAPs live in the safe zone 0x7C00-0x7DAA.
# Memory: 0x6000 VBE mode info; 0x7000 boot info; 0x7C00 boot sector;
# 0x7E00 boot vars; 0x20000 kernel; 0x400000 heap.
.code16
.org 0
.section .text
.globl _start
.equ BOOT_DRIVE, 0x7E00

_start:
    jmp start_code
    nop

# ---- protected mode entry (32-bit, at 0x7C04, clear of BIOS clobber zone) ----
.org 0x04
.code32
pm_entry:
    movw $0xE9, %dx
    movb $'K', %al
    outb %al, %dx          # probe K: PM entered
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    movl $0x1F000, %esp
    ljmp $0x18, $0x20000   # flat code -> kernel entry

# ---- constants zone (0x7C30-0x7C5D, clear of BIOS clobber zone 0x7DAA+) ----
.code16
.org 0x30
gdtr:
    .word gdt_end - gdt - 1
    .long gdt + 0x7C00
gdt:
    .quad 0x0000000000000000           # 0x00 null
    .quad 0x00CF9A000000FFFF           # 0x08 code32 flat
    .quad 0x00CF92000000FFFF           # 0x10 data32 flat
    .quad 0x00CF9A000000FFFF           # 0x18 code32 flat
gdt_end:
.org 0x58
kernel_count: .word 0                  # kernel sector count (patched at 0x7C58)

.org 0x60
start_code:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp
    movb %dl, BOOT_DRIVE
    sti

    # ---- set VBE 800x600x32 (0x4118) ----
    movw $0x4F02, %ax
    movw $0x4118, %bx
    int $0x10
    cmpw $0x004F, %ax
    jne vbe_fail
    call dbg_char
    .byte 'V'
    movw $0x4F01, %ax
    movw $0x4118, %cx
    xorw %di, %di
    movw %di, %es
    movw $0x6000, %di
    int $0x10
    cmpw $0x004F, %ax
    jne vbe_fail
    xorw %ax, %ax
    movw %ax, %es
    movl 0x6028, %eax            # phys base ptr (LFB)
    movl %eax, 0x7000
    movl $800, 0x7004
    movl $600, 0x7008
    movl $3200, 0x700C
    # query current VBE mode (0x4F03) -> probe '1' if 0x118 took, else '0'
    movw $0x4F03, %ax
    xorw %bx, %bx
    int $0x10
    andw $0x0FFF, %bx
    cmpw $0x0118, %bx
    jne vbe_mode_nok
    call dbg_char
    .byte '1'
    jmp vbe_mode_done
vbe_mode_nok:
    call dbg_char
    .byte '0'
vbe_mode_done:
    nop

# ---- load kernel.bin from real ATAPI CD via EDD int 0x13 AH=0x42 ----
# kernel.bin at physical LBA 24, 57 x 2048B sectors, two static chunks.
cd_load_kernel:
    movw $cdap1 + 0x7C00, %si
    movb BOOT_DRIVE, %dl
    movb $0x42, %ah
    int $0x13
    jc cd_retry
    call dbg_char
    .byte '1'
    movw $cdap2 + 0x7C00, %si
    movb BOOT_DRIVE, %dl
    movb $0x42, %ah
    int $0x13
    jc cd_retry
    call dbg_char
    .byte '2'
    movw $cdap3 + 0x7C00, %si
    movb BOOT_DRIVE, %dl
    movb $0x42, %ah
    int $0x13
    jc cd_retry
    call dbg_char
    .byte '3'
    movw $cdap4 + 0x7C00, %si
    movb BOOT_DRIVE, %dl
    movb $0x42, %ah
    int $0x13
    jc cd_retry
    call dbg_char
    .byte '4'
    call dbg_char
    .byte 'L'                 # all chunks ok
    jmp kernel_loaded

cd_retry:
    call dbg_char
    .byte 'C'                 # failed, reset and retry whole load
    xorw %ax, %ax
    int $0x13
    jmp cd_load_kernel

# ---- debugcon probe: prints the .byte right after 'call dbg_char' ----
dbg_char:
    popw %di                  # di = return address (points at the .byte)
    movb (%di), %al
    movw $0xE9, %dx
    outb %al, %dx
    incw %di
    jmp *%di                  # resume after the .byte

kernel_loaded:
    # ---- protected mode (A20 already enabled by BIOS on real hw + QEMU) ----
    cli
    lgdt gdtr + 0x7C00
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmp $0x08, $0x7C04

# ---- EDD DAPs (static; fully clear of BIOS clobber zone 0x7DAA+) ----
.org 0x170
cdap1:
    .byte 0x10, 0x00      # size
    .word 32              # count (2048B sectors, 64KB)
    .word 0x0000          # offset
    .word 0x2000          # segment 0x2000 -> 0x20000
    .long 24              # lba low (kernel starts at LBA24)
    .long 0               # lba high
cdap2:
    .byte 0x10, 0x00      # size
    .word 32              # count (chunk2 2048B sectors)
    .word 0x0000          # offset
    .word 0x3000          # segment 0x3000 -> 0x30000
    .long 56              # lba low (24+32)
    .long 0               # lba high
cdap3:
    .byte 0x10, 0x00      # size
    .word 32              # count (chunk3 2048B sectors)
    .word 0x0000          # offset
    .word 0x4000          # segment 0x4000 -> 0x40000
    .long 88              # lba low (24+64)
    .long 0               # lba high
cdap4:
    .byte 0x10, 0x00      # size
    .word 32              # count (128 total = 262144B, kernel 246272B)
    .word 0x0000          # offset
    .word 0x5000          # segment 0x5000 -> 0x50000
    .long 120             # lba low (24+96)
    .long 0               # lba high

.org 0x1FE
    .word 0xAA55
