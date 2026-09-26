# nefuOS bare boot sector (El Torito no-emulation, DL=0xE0 real ATAPI CD)
# Duties: load the 2-sector multi-boot picker (menu.bin @ LBA 24) to 0x10000
# and jump to it; the picker detects a second bootable OS on the hard disk
# and either boots nefuOS or warm-reboots into Windows -> VBE 1024x768x32 ->
# store LFB/bootinfo at 0x7000 -> PING-PONG load the compressed payload from
# CD LBA26 in 512KB windows -> protected mode -> stub copies each window to a
# 0x620000 staging area -> real mode (via the 0x8000 PM->RM trampoline) ->
# next window -> final window: stub inflates the kernel to 0x100000 ->
# long mode entry (entry.s).
# Why ping-pong: the real-mode loader can only reach ~512KB contiguously
# (0x20000-0xA0000, below the VGA/ROM hole), but the QuickJS kernel payload
# is ~1MB+.  boot.s keeps filling the 0x20000 window, switching to PM between
# windows so the stub can relocate each chunk out of the way.  boot.s stays in
# charge of I/O; the stub only copies / inflates / hands control back.
#
# Key design:
#  1) SeaBIOS no-emulation: DL=0xE0, physical 2048B sectors. int13 AH=0x42
#     is served by the real ATAPI stack, single call <= 32 sectors (64KB).
#  2) one DAP at 0x7C16 is rewritten per call (count <= 32 CD sectors = 64KB),
#     advancing LBA/segment/remain/loaded.  When the 0x20000-0xA0000 window
#     is full (256 CD sectors) or the payload is exhausted, boot.s switches
#     to PM and far-jumps (push sel; push off32; lret) to the stub.
#  3) Stub returns to real mode via a PM far jump to a trampoline that
#     boot.s installs at 0x8000 (below the 64KB real-mode CS limit): it
#     clears CR0.PE and far-jumps into cd_resume, which resets the window
#     base and keeps loading.
#  4) SeaBIOS clobbers the boot-sector tail 0x7D80+ during later windows
#     (empirically confirmed), so GDT/kernel_count/state/DAP/PM slots all
#     live in the safe zone 0x7C00-0x7D7F (cross-window state at 0x7C04).
# Layout (offsets): 0x00 entry, 0x04 pm slots+DAP+CD state (cross-window,
# 0x30 gdtr/gdt, 0x58 kernel_count, 0x60 start_code (VBE + CD load),
# 0xC1 cd_load_kernel, 0x1A1 menu DAP, 0x1C0 bss info, 0x1C8 menu code,
# 0x1FE 0xAA55.
.code16
.org 0
.section .text
.globl _start
.equ BOOT_DRIVE, 0x7E00

_start:
    jmp start_code
    nop

# ---- cross-window state (0x7C04-0x7C24).  SeaBIOS clobbers the boot-sector
# tail (0x7D80+) during the later CD windows (observed: final_flag -> 0x0A,
# cd_rem -> 0x288D, pm_off -> garbage), so every value that must survive the
# boot.s <-> stub ping-pong lives HERE, below the GDT (which demonstrably
# survives: window 2's lgdt still worked).  The legacy pm_entry at 0x04 is
# retired (unused dead code) and replaced by these slots.
.org 0x04
pm_off:      .long 0x20000    # 32-bit stub entry offset (stub updates to 0x620000)
pm_sel:      .word 0x0008     # code32 flat selector
real_resume: .long cd_resume + 0x7C00   # real-mode return target (offset)
real_sel:    .word 0x0018     # code16 selector (GDT index 3 -> 0x18)
.org 0x10
final_flag:  .byte 0          # 1 => last chunk (unused; stub decides via cd_rem)
cd_rem:      .word 0          # remaining 2048B CD sectors (cross-window)
cd_loaded_s: .word 0          # CD sectors loaded into the current window (<=256)

# ---- EDD DAP for the payload loop (in the safe zone; rewritten per call) ----
.org 0x16
cdap1:
    .byte 0x10, 0x00   # size
cdap1_count: .word 32  # count (2048B sectors, max 32 = 64KB)
    cdap1_off: .word 0x0000  # offset
cdap1_seg: .word 0x2000  # segment -> physical 0x20000
cdap1_lba: .long 26    # lba low (payload starts at LBA 26)
    .long 0            # lba high

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
    .quad 0x00009A000000FFFF           # 0x18 code16 (base 0, 64KB, D=0; idx 3)
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
    jmp vbe_setup                # program the display, then the boot menu

    # ---- VBE: program the display controller directly through the bochs
    # VBE registers (0x1CE/0x1CF).  QEMU's stdvga maps BIOS VBE modes to
    # 24bpp (unusable pixel layout); direct register setup yields a true
    # 32bpp linear framebuffer.  LFB base is QEMU stdvga BAR0.
vbe_setup:
    movl $0xFD000000, %eax
    movl %eax, 0x7000
    # loop over (index, value) pairs: XRES, YRES, BPP, ENABLE
    # labels are file offsets; add 0x7C00 so DS:SI/DS:DI reach runtime data
    leaw vbe_idx + 0x7C00, %si
    leaw vbe_val + 0x7C00, %di
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
    movw $1024, 0x7004
    movw $768, 0x7008
    movw $4096, 0x700C
    movb $32, 0x7010
    jmp menu
vbe_idx: .word 1, 2, 3, 4
vbe_val: .word 1024, 768, 32, 0x0081

# ===================== ping-pong CD loader =====================
# menu.s hardcodes a far jump back here (0:0x7CC1 = this label), so this label
# is pinned at byte offset 0xC1 regardless of what grows above it.
.org 0xC1
cd_load_kernel:
    # rem = ceil(kernel_count / 4)  (512B kernel sectors -> 2048B CD sectors)
    movw kernel_count + 0x7C00, %ax
    addw $3, %ax
    shrw $2, %ax
    movw %ax, cd_rem + 0x7C00
    movw $0, cd_loaded_s + 0x7C00
cd_loop:
    movw cd_rem + 0x7C00, %ax
    testw %ax, %ax
    jz  cd_last                  # payload exhausted: final stub call
    cmpw $256, cd_loaded_s + 0x7C00
    je  cd_flush0                # 512KB window full: flush, keep loading
    cmpw $32, cd_rem + 0x7C00
    jbe 1f
    movw $32, %ax                # max 32 CD sectors (64KB) per int13 call
1:
    movw %ax, cdap1_count + 0x7C00
    movw $cdap1 + 0x7C00, %si
    movb BOOT_DRIVE, %dl
    movb $0x42, %ah
    int $0x13
    jc cd_retry
    # advance: lba += n, seg += n*0x80, loaded_s += n, rem -= n
    movw cdap1_count + 0x7C00, %cx
    movzwl %cx, %eax
    addl %eax, cdap1_lba + 0x7C00
    movw %cx, %ax
    shlw $7, %ax
    addw %ax, cdap1_seg + 0x7C00
    addw %cx, cd_loaded_s + 0x7C00
    subw %cx, cd_rem + 0x7C00
    jmp cd_loop
cd_last:
    jmp cd_pm
cd_flush0:
cd_pm:
    cli
    lgdt gdtr + 0x7C00
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    # probe P: about to far-jump (lret) to the stub
    movw $0xE9, %dx
    movb $'P', %al
    outb %al, %dx
    # far jump to the stub (pm_off/pm_sel).  In 16-bit mode an indirect
    # far jmp only takes m16:16, but 0x20000/0x620000 need 32-bit: push the
    # selector then the 32-bit offset and lret with a data32 prefix.
    pushw pm_sel + 0x7C00
    pushl pm_off + 0x7C00
    data32 lret                 # 66h prefix: pop EIP32 (0x20000/0x300000) + CS16
# the stub returns here (real mode, DS/SS restored below) after each window copy
cd_resume:
    # probe R: stub returned to real mode via the 0x8000 trampoline
    movw $0xE9, %dx
    movb $'R', %al
    outb %al, %dx
    sti
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %ss
    movw $0x7C00, %sp
    movw $0x2000, cdap1_seg + 0x7C00
    movw $0, cdap1_off + 0x7C00
    movw $0, cd_loaded_s + 0x7C00
    jmp cd_loop
cd_retry:
    xorw %ax, %ax
    int $0x13
    jmp cd_loop

# ---- EDD DAP for the payload loop (rewritten per call; clear of clobber).
# menu.s uses the separate menu_dap below, so these initializers survive.
# (The PM->RM trampoline installer lives in menu.s: it must run before the
# first cd_pm, and menu.s always runs first; boot.s has no room left.)
# NOTE: the payload DAP itself lives at 0x7C16 in the safe zone (see top);
# the block below is retired.  The menu loader DAP follows at 0x1A1.
# ---- menu.bin loader DAP (separate from the payload DAP so the payload DAP
# stays pristine across the menu round-trip; LBA 24, 2 sectors to 0x10000) ----
.org 0x1A1
menu_dap:
    .byte 0x10, 0x00
menu_cnt: .word 2
menu_off: .word 0x0000
menu_seg: .word 0x1000
menu_lba: .long 24
    .long 0

# ---- bss zero-fill info (patched at build time) ----
# entry.s reads these to know where to rep stosq the kernel .bss.
# mingw ld PE symbols for __bss_start/__bss_end are RVAs (0), so the
# runtime address is patched here from the PE section table instead.
.org 0x1C0
bss_start: .long 0        # absolute runtime address of .bss (0x100000 + RVA)
bss_size:  .long 0        # .bss VirtualSize in bytes

# ---- boot menu code (fits the free slot 0x1C8-0x1FE) ----
# Loads the 2-sector multi-boot picker (menu.bin @ CD LBA 24) to 0x10000 and
# jumps to it. The picker detects whether the hard disk holds another
# bootable OS; single-boot machines jump straight back to cd_load_kernel.
.org 0x1C8
menu:
    movw $2, menu_cnt + 0x7C00
    movw $0x1000, menu_seg + 0x7C00
    movw $menu_dap + 0x7C00, %si
    movb BOOT_DRIVE, %dl
    movb $0x42, %ah
    int $0x13
    jc m_retry
    .byte 0xEA                       # ljmp 0x1000:0x0000 -> menu.s
    .word 0x0000
    .word 0x1000
m_retry:
    xorw %ax, %ax
    int $0x13
    jmp menu
# menu.s returns here via ljmp 0:0x7CC1 (cd_load_kernel absolute address).

.org 0x1FE
    .word 0xAA55
