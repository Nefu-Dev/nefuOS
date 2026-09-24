# nefuOS multi-boot picker — 16-bit real-mode boot-stage OS selector.
# Loaded by boot.s at 0x10000 (2 CD sectors @ LBA 24) with a single EDD DAP
# rewrite, entered with CS=0x1000:0x0000, DS=0, SS:SP = 0:0x7C00 (the boot.s
# stack, untouched by this stage).  Immediately after entry DS is switched
# to 0x1000 so every label below (file offset) addresses 0x10000 + offset.
#
# Behavior (Windows-Boot-Manager style):
#   1) detect a bootable primary hard disk (int13 AH=15 existence check +
#      EDD MBR read + 0xAA55 signature + non-empty partition table)
#   2) no second boot item found -> jump straight back into boot.s
#      cd_load_kernel (0x7CC1) and boot nefuOS (single boot, zero delay)
#   3) hard disk looks bootable -> render an 80x25 blue picker:
#        nefuOS (CD)          <- default, highlighted (white bar)
#        Windows (Hard Disk)  <- shown only when detected
#      UP/DOWN move the highlight, ENTER chooses, 3 s timeout -> nefuOS
#   4) Windows -> int19 (BIOS reboot -> hard disk). nefuOS -> 0x7CC1.
#
# debugcon probes (isa-debugcon @ 0xE9): M = picker shown,
#   N = nefuOS chosen / single boot, W = Windows chosen.
.code16
.org 0
.section .text
.globl menu_start

menu_start:
    jmp main16

# ---- variables (file offsets; DS=0x1000 -> 0x10000 + offset) ----
.org 0x40
sel_idx:  .byte 0            # 0 = nefuOS (default), 1 = Windows
sec_left: .byte 3            # countdown seconds

.org 0x48
dap:                        # EDD DAP for the MBR probe (ds:si)
    .byte 0x10, 0x00
dap_cnt: .word 1
dap_off: .word 0x0000
dap_seg: .word 0x07E2       # buffer 0x7E20 (below boot.s state, above BOOT_DRIVE)
dap_lba: .long 0            # LBA 0 = MBR
    .long 0

# ---- entry ----
.org 0x60
main16:
    cld
    movw $0x1000, %ax
    movw %ax, %ds           # address this stage's data at 0x10000
    movw $0x0003, %ax       # 80x25 text mode (idempotent)
    int $0x10
    call detect_hd          # ax = 1 multi-boot, 0 single
    testw %ax, %ax
    jz  boot_nefuos         # single boot -> straight to nefuOS, no page
    movb $0, sel_idx        # default highlight: nefuOS
    call render             # draw the full picker page
    call probe
    .byte 'M'               # picker shown (multi-boot detected)
    call wait_keys          # ENTER / timeout; sel_idx holds the choice
    cmpb $1, sel_idx
    je  boot_windows
    jmp boot_nefuos

# ---- probe helper: emits the .byte right after 'call probe' to debugcon ----
# Uses CS (this stage's code segment = 0x1000) for the byte read so it works
# even when DS was switched away to inspect low memory.
probe:
    popw %di                # di = return address (points at the .byte)
    movb %cs:(%di), %al
    movw $0xE9, %dx
    outb %al, %dx
    incw %di
    jmp *%di                # resume after the .byte

# ---- detect: is the primary hard disk bootable? ----
detect_hd:
    movb $0x80, %dl
    movw $0x1500, %ax       # AH=15: get disk type
    int $0x13
    jc  .no                 # drive absent
    testb %al, %al
    jz  .no                 # type 0 = no device
    movw $dap, %si          # ds = 0x1000 -> DAP at 0x10048
    movb $0x80, %dl
    movw $0x4200, %ax       # AH=42: EDD extended read of LBA 0
    int $0x13
    jc  .no                 # EDD unavailable -> be safe, single boot
    pushw %ds
    xorw %ax, %ax
    movw %ax, %ds           # ds=0 to inspect the 0x7E20 MBR buffer
    cmpw $0xAA55, 0x801E    # MBR boot signature @ 0x7E20 + 0x1FE
    jne .no2
    movw $0x7FDE, %si       # partition table 0x7E20 + 0x1BE .. + 0x1FE
    movw $64, %cx
.pt:
    cmpb $0, (%si)
    jne .yes2               # any non-zero byte -> bootable partition exists
    incw %si
    loop .pt
.no2:
    popw %ds
    xorw %ax, %ax
    ret
.yes2:
    popw %ds
    movw $1, %ax
    ret

# ---- full page render (80x25, blue background) ----
render:
    movw $0xB800, %ax
    movw %ax, %es
    xorw %di, %di
    movw $2000, %cx
    movw $0x1F20, %ax       # space, attr 0x1F (blue bg / bright-white fg)
    rep stosw
    movw $160, %di          # row 1: title
    movw $str_title, %si
    movb $0x1F, %ah
    call put_str
    movw $480, %di          # row 3: prompt
    movw $str_prompt, %si
    movb $0x1F, %ah
    call put_str
    movw $1440, %di         # row 9: hint
    movw $str_hint, %si
    movb $0x1F, %ah
    call put_str
    movw $1760, %di         # row 11: countdown label
    movw $str_secs, %si
    movb $0x1F, %ah
    call put_str
    call render_items
    call render_secs
    ret

# ---- item rows with the highlight bar ----
render_items:
    movw $0xB800, %ax
    movw %ax, %es
    movw $800, %di          # row 5: nefuOS (CD)
    movw $str_item0, %si
    cmpb $0, sel_idx
    jne .i0n
    movb $0x70, %ah         # highlighted: white bg / black text
    jmp .w0
.i0n:
    movb $0x1F, %ah
.w0:
    call put_str
    movw $960, %di          # row 6: Windows (Hard Disk)
    movw $str_item1, %si
    cmpb $1, sel_idx
    jne .i1n
    movb $0x70, %ah
    jmp .w1
.i1n:
    movb $0x1F, %ah
.w1:
    call put_str
    ret

# ---- countdown digit at row 11, col 67 ----
render_secs:
    movw $0xB800, %ax
    movw %ax, %es
    movw $1894, %di         # (11*80 + 67) * 2
    movb sec_left, %al
    addb $'0', %al
    movb $0x1F, %ah
    movw %ax, %es:(%di)
    ret

# ---- put string: es:di target, ds:si source, ah = attribute ----
put_str:
    lodsb
    testb %al, %al
    jz  .done
    movw %ax, %es:(%di)
    incw %di
    incw %di
    jmp put_str
.done:
    ret

# ---- wait for a choice: 3 s countdown, UP/DOWN, ENTER ----
wait_keys:
    movb $3, sec_left
.tick:
    call render_secs
    movw $0x8600, %ax       # int15 AH=86: delay 1 s
    movw $0x000F, %cx
    movw $0x4240, %dx
    int $0x15
    movw $0x0100, %ax       # any key pending?
    int $0x16
    jnz .key
    decb sec_left
    jnz .tick
    ret                     # timeout: sel_idx unchanged (0 = nefuOS)
.key:
    xorw %ax, %ax
    int $0x16
    cmpb $0x48, %ah         # Up scan code
    je  .up
    cmpb $0x50, %ah         # Down scan code
    je  .down
    cmpb $0x1C, %ah         # ENTER scan code
    je  .enter
    jmp wait_keys           # ignore other keys, restart countdown
.up:
    movb $0, sel_idx
    call render_items
    jmp wait_keys
.down:
    movb $1, sel_idx
    call render_items
    jmp wait_keys
.enter:
    ret

# ---- choices ----
boot_windows:
    call probe
    .byte 'W'
    xorw %ax, %ax
    movw %ax, %ds           # restore DS=0 for the BIOS reboot
    int $0x19               # BIOS reboot -> hard disk -> Windows
    jmp boot_nefuos         # int19 should not return; safe fallback
boot_nefuos:
    call probe
    .byte 'N'
    xorw %ax, %ax
    movw %ax, %ds           # restore DS=0: boot.s cd_load_kernel reads 0x7Cxx
    .byte 0xEA              # ljmp 0:0x7CC1 -> boot.s cd_load_kernel
    .word 0x7CC1
    .word 0x0000

# ---- strings (ASCII only; BIOS text mode has no CJK font) ----
.org 0x300
str_title:
    .ascii "          nefuOS Boot Manager"
    .byte 0
str_prompt:
    .ascii "   Choose an operating system to start:"
    .byte 0
str_item0:
    .ascii "   nefuOS (CD)"
    .byte 0
str_item1:
    .ascii "   Windows (Hard Disk)"
    .byte 0
str_hint:
    .ascii "   Use the UP/DOWN arrow keys to move the highlight, press ENTER to choose."
    .byte 0
str_secs:
    .ascii "   Seconds until the highlighted choice is started automatically:  "
    .byte 0
