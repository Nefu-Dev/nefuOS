# nefuOS bare boot sector (El Torito no-emulation, DL=0xE0 real ATAPI CD)
# Duties: VBE 1024x768x32 -> store LFB/bootinfo at 0x7000 -> load kernel.bin
# from CD LBA24 (any size, dynamic loop) to 0x20000 -> protected mode ->
# long mode entry (entry.s) -> nefuos_kernel_main.
# Key design:
#  1) SeaBIOS no-emulation: DL=0xE0, physical 2048B sectors. int13 AH=0x42
#     is served by the real ATAPI stack, single call <= 32 sectors (64KB).
#  2) kernel.bin is loaded by a dynamic loop: one DAP at 0x7D70 is rewritten
#     per call (count <= 32 CD sectors = 64KB), advancing LBA/segment/remain
#     until kernel_count/4 sectors are read.  DAP/state live below the
#     SeaBIOS clobber zone 0x7DAA.
#  3) After each chunk, advance state; a CF=1 failure resets and retries the
#     whole load.
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

    # ---- VBE: program the display controller directly through the bochs
    # VBE registers (0x1CE/0x1CF).  QEMU's stdvga maps BIOS VBE modes to
    # 24bpp (unusable pixel layout); direct register setup yields a true
    # 32bpp linear framebuffer.  LFB base is QEMU stdvga BAR0.
    movl $0xFD000000, %eax
    movl %eax, 0x7000
    call dbg_char
    .byte 'V'
    # loop over (index, value) pairs: XRES, YRES, BPP, ENABLE
    leaw vbe_idx, %si
    leaw vbe_val, %di
    movw $4, %cx
vbe_loop:
    movw $0x1CE, %dx
    lodsw
    outw %ax, %dx
    movw $0x1CF, %dx
    movw (%di), %ax
    outw %ax, %dx
    incw %di
    incw %di
    loop vbe_loop
    # bootinfo: X=1024 Y=768 pitch=4096 bpp=32
    movl $1024, 0x7004
    movl $768, 0x7008
    movl $4096, 0x700C
    movb $32, 0x7010
    jmp vbe_done
vbe_idx: .word 1, 2, 3, 4
vbe_val: .word 1024, 768, 32, 0x0081
vbe_done:

# ---- load kernel.bin from real ATAPI CD via EDD int 0x13 AH=0x42 ----
# kernel.bin starts at physical LBA 24 (make_iso.py layout). CD sectors are
# 2048B; int13 0x42 is limited to 64KB (32 sectors) per call, so we loop.
# State lives in memory (0x7D80..), never in BP/SI (int13 0x42 clobbers
# those); the single DAP at 0x7D70 is rewritten before every call and sits
# well below SeaBIOS's VBE clobber zone (0x7DAA+).
cd_load_kernel:
    # rem = ceil(kernel_count / 4)  (512B kernel sectors -> 2048B CD sectors)
    movw kernel_count, %ax
    addw $3, %ax
    shrw $2, %ax
    movw %ax, cd_rem
    movl $24, %eax
    movl %eax, cd_lba
    movw $0x2000, %ax
    movw %ax, cd_seg
cd_loop:
    cmpw $0, cd_rem
    jz  cd_done
    movw cd_rem, %ax
    cmpw $32, %ax
    jbe cd_n_ok
    movw $32, %ax
cd_n_ok:
    movw %ax, cdap1_count
    movw cd_seg, %ax
    movw %ax, cdap1_seg
    movl cd_lba, %eax
    movl %eax, cdap1_lba
    movw $cdap1 + 0x7C00, %si
    movb BOOT_DRIVE, %dl
    movb $0x42, %ah
    int $0x13
    jc cd_retry
    # advance: lba += n, seg += n*0x80 (n sectors * 2048B / 16), rem -= n
    movw cdap1_count, %cx
    movzwl %cx, %edx
    movl cd_lba, %eax
    addl %edx, %eax
    movl %eax, cd_lba
    movw %cx, %ax
    shlw $7, %ax
    addw %ax, cd_seg
    subw %cx, cd_rem
    jmp cd_loop
cd_done:
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

# ---- EDD DAP (static slot, fields rewritten per call; clear of clobber) ----
# Dynamic loader code ends ~0x164; DAP + state sit below the SeaBIOS
# clobber zone 0x7DAA (= offset 0x1AA), same as the committed 6-DAP layout.
.org 0x170
cdap1:
    .byte 0x10, 0x00   # size
cdap1_count: .word 32  # count (2048B sectors, max 32 = 64KB)
    .word 0x0000       # offset
cdap1_seg: .word 0x2000  # segment -> physical 0x20000
cdap1_lba: .long 24    # lba low (kernel starts at LBA24)
    .long 0            # lba high

# ---- CD load state (0x7D80, clear of SeaBIOS clobber zone 0x7DAA+) ----
.org 0x180
cd_rem:  .word 0       # remaining 2048B CD sectors
cd_lba:  .long 0       # current physical LBA (48-bit, low 32 used)
cd_seg:  .word 0       # current load segment

# ---- bss zero-fill info (patched at build time) ----
# entry.s reads these to know where to rep stosq the kernel .bss.
# mingw ld PE symbols for __bss_start/__bss_end are RVAs (0), so the
# runtime address is patched here from the PE section table instead.
.org 0x1C0
bss_start: .long 0        # absolute runtime address of .bss (0x20000 + RVA)
bss_size:  .long 0        # .bss VirtualSize in bytes

.org 0x1FE
    .word 0xAA55
