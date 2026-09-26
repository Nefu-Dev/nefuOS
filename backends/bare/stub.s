# nefuOS boot stub — 32-bit protected-mode, position-independent.
# Ping-pong loader partner of boot.s: runs first at 0x20000 (window 1), then,
# after copying itself out of the load window, at 0x620000 for every later
# window.  boot.s far-jumps here via `pushw pm_sel; pushl pm_off; data32 lret`
# (pm_off = 0x20000 initially, updated to 0x620000 by this stub on pass 1).
#
# Payload layout (built by tools/build_iso.ps1):
#   +0x00  this stub + inflate code (linked at VMA 0x620000, so inflate's
#          absolute references to its const tables are correct once window 1
#          has been copied to 0x620000; the stub's own references are all
#          base-relative, so it works from 0x20000 or 0x620000 alike)
#   +0x80  kernel_src_off (u32)  <- offset of compressed kernel in payload
#   +0x84  kernel_src_len (u32)  <- compressed kernel size (bytes)
#   +0x88  kernel_dst_cap (u32)  <- decompressed capacity (bytes)
#   +0x8C  copy_dst (u32)        <- next staging destination (init 0x620000)
#   +0x90  inflate code, then the compressed kernel (raw DEFLATE, wbits=-15)
#
# Each pass: copy the 512KB window 0x20000..0xA0000 to copy_dst (staging slots
# 0x620000, 0x6A0000, 0x720000, ... so the payload is reassembled contiguously),
# advance copy_dst in the running copy, point pm_off (boot.s slot 0x7C04) at
# 0x620000 on the first pass, then either far-jump to the 0x8000 PM->RM
# trampoline (boot.s installs it; clears CR0.PE and lands in cd_resume for the
# next window) or, when boot.s's cd_rem slot (0x7C10) hits 0, inflate the whole
# payload from staging to 0x100000 and ljmp to the kernel (entry.s).
#
# SeaBIOS clobbers the boot-sector tail 0x7D80+ across windows (final_flag ->
# 0x0A, cd_rem -> 0x288D, pm_off -> garbage all observed), so boot.s moved all
# cross-window state to 0x7C04-0x7C24 (below the GDT, which survives).  The
# stub therefore reads/writes ONLY those safe addresses.
#
# Slot offsets are numeric (.equ) so the code stays position-independent even
# though the linker places everything at VMA 0x620000.  The entry block must
# stay below 0x80 so the patched slots keep their fixed byte offsets.
.equ SLOT_SRC_OFF,  0x80
.equ SLOT_SRC_LEN,  0x84
.equ SLOT_DST_CAP,  0x88
.equ SLOT_COPY_DST, 0x8C
.equ SLOT_CODE,     0x90       # code resumes here, after the 4 slots
.equ WIN_SZ,        0x80000      # 512KB load window
.equ WIN_BASE,      0x20000
# Staging sits ABOVE the kernel image: the inflate destination is
# 0x100000..0x100000+kSize (kSize ~5.2MB, ends ~0x5EA200), so a staging base
# below that would let the decompressor overwrite the not-yet-consumed
# compressed source (observed as inflate error -24).  0x620000 clears the
# kernel end (enforced by the build's memmap check); 5 windows of 512KB end at
# 0x8A0000, below the 0x900000 stacks (stub + entry).
.equ STAGE_BASE,    0x620000
.equ STAGE_STACK,   0x900000

.code32
.section .text
.org 0
.globl stub_start
stub_start:
    # ---- compute the runtime base (0x20000 first pass, 0x620000 after) ----
    call base_probe
base_probe:
    popl %ebx
    subl $base_probe - stub_start, %ebx
    # ---- flat segments + a high stack (clear of staging/kernel) ----
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    movl $STAGE_STACK, %esp
    # probe S: stub entered (PM, window loaded)
    movw $0xE9, %dx
    movb $'S', %al
    outb %al, %dx
    # NOTE: the runtime-base hex print lives after the staging/dump block
    # (below); the entry must stay < 0x80 for the patched slots.
    # jump OVER the build-time slot area (0x80..0x90) into the real code at
    # 0x90.  NEVER fall through into the .org padding/slot bytes.
    jmp slotted_code

# ---- build-time patched slots (byte offsets patched by tools/build_iso.ps1) ----
.org SLOT_SRC_OFF
kernel_src_off: .long 0
kernel_src_len: .long 0
kernel_dst_cap: .long 0
copy_dst:       .long STAGE_BASE   # first staging destination (pass 1)

.org SLOT_CODE
slotted_code:
    # ---- first pass only: boot.s must jump to the relocated stub next ----
    cmpl $WIN_BASE, %ebx
    jne 1f
    movl $STAGE_BASE, 0x7C04    # pm_off slot (boot.s reads it at 0x7C00+0x04)
1:
    # ---- stage this window: copy 0x20000..0xA0000 to copy_dst ----
    movl SLOT_COPY_DST(%ebx), %edi
    movl $WIN_BASE, %esi
    movl $WIN_SZ, %ecx
    cld
    rep movsb                   # edi now = dst + 0x80000 (next staging base)
    # ---- advance copy_dst in the running copy (0x20000 pass 1, 0x620000+) ----
    movl SLOT_COPY_DST(%ebx), %eax
    addl $WIN_SZ, %eax
    movl %eax, SLOT_COPY_DST(%ebx)
    # first pass only: the freshly-relocated copy at 0x620000 must also see
    # the advanced value, or pass 2 would re-stage the next window on top of
    # window 1.  STAGE_BASE + SLOT_COPY_DST is a fixed physical address.
    cmpl $WIN_BASE, %ebx
    jne 2f
    movl %eax, (STAGE_BASE + SLOT_COPY_DST)
2:
    # probe C: window staged
    movw $0xE9, %dx
    movb $'C', %al
    outb %al, %dx
    # probe D + hex dump.  Three regions:
    #   a) boot.s cross-window slots 0x7C04..0x7C24 (pm_off, pm_sel,
    #      real_resume, real_sel, final_flag, cd_rem, cd_loaded_s, cdap1)
    #      -- these MUST survive each window (safe zone below the GDT);
    #   b) this stub's own copy_dst slot (relocated image at 0x620000+0x8C)
    #      -- should read 0x6A0000 on window 1, 0x720000 on window 2;
    #   c) the old clobber zone 0x7D80..0x7D84 -- expected garbage, kept as
    #      proof that the BIOS rewrite still happens there.
    movw $0xE9, %dx
    movb $'D', %al
    outb %al, %dx
    movl $0x7C04, %esi
    movl $33, %ecx
    call dump_hex
    movl $(STAGE_BASE + SLOT_COPY_DST), %esi
    movl $4, %ecx
    call dump_hex
    movl $0x7D80, %esi
    movl $5, %ecx
    call dump_hex
    # probe B + runtime base as 8 hex digits (0x00020000 / 0x00620000)
    movw $0xE9, %dx
    movb $'B', %al
    outb %al, %dx
    movl %ebx, %esi
    movl $8, %ecx
.base_hex:
    roll $4, %esi
    movl %esi, %eax
    andl $15, %eax
    cmpl $10, %eax
    jb .base_d
    addl $7, %eax
.base_d:
    addl $'0', %eax
    movw $0xE9, %dx
    outb %al, %dx
    decl %ecx
    jnz .base_hex
    # ---- final pass?  boot.s's cd_rem hits 0 exactly when the payload is
    # exhausted.  It lives in the safe zone (0x7C10) so BIOS cannot corrupt it
    # across windows (the old 0x7DA0 final_flag / 0x7D80 cd_rem both got
    # clobbered, causing premature inflate / lost windows). ----
    movw 0x7C10, %ax
    testw %ax, %ax
    jz do_inflate
    # ---- not final: back to real mode for the next window ----
    # Far-jump (PM) to the PM->RM trampoline at 0x8000 that boot.s installs:
    # it clears CR0.PE and far-jumps to cd_resume.  We can NOT clear PE here
    # in place: once PE=0 the CPU is in real mode and our EIP (~0x200B0 /
    # 0x6200B0) exceeds the 64KB real-mode CS limit -> #GP on the next fetch.
    ljmp $0x08, $0x8000       # PM flat jump to the trampoline (below 64K)
do_inflate:
    # ---- last window staged: inflate the whole payload from 0x620000 ----
    # cdecl: _nefu_inflate(dst, dst_cap, src, src_len); args pushed right-to-left
    movl SLOT_SRC_LEN(%ebx), %eax
    pushl %eax
    movl SLOT_SRC_OFF(%ebx), %eax
    addl $STAGE_BASE, %eax
    pushl %eax
    movl SLOT_DST_CAP(%ebx), %eax
    pushl %eax
    pushl $0x100000
    call _nefu_inflate
    addl $16, %esp
    movl %eax, %ebx             # save result for error reporting
    testl %eax, %eax
    jle stub_fail
    # probe G: decompression succeeded (eax = byte count)
    movw $0xE9, %dx
    movb $'G', %al
    outb %al, %dx
    ljmp $0x08, $0x100000       # flat code32 -> kernel entry (entry.s)
stub_fail:
    # probe '!': inflate returned error (eax negative or zero)
    movw $0xE9, %dx
    movb $'!', %al
    outb %al, %dx
    # print ebx as 8 hex digits via a compact loop
    movl %ebx, %esi
    movl $8, %ecx
.hexl:
    roll $4, %esi
    movl %esi, %eax
    andl $15, %eax
    cmpl $10, %eax
    jb .hd
    addl $7, %eax
.hd:
    addl $'0', %eax
    movw $0xE9, %dx
    outb %al, %dx
    decl %ecx
    jnz .hexl
.halt:
    cli
    hlt
    jmp .halt

# ---- diagnostic: dump ecx bytes at esi as hex to debugcon (2 chars each) ----
dump_hex:
    pushl %ecx
.dh_loop:
    movb (%esi), %al
    pushl %ecx
    call .dh_byte
    popl %ecx
    incl %esi
    loop .dh_loop
    popl %ecx
    ret
.dh_byte:
    pushl %eax
    shrb $4, %al
    call .dh_nib
    popl %eax
    andb $15, %al
    call .dh_nib
    ret
.dh_nib:
    cmpb $10, %al
    jb  .dh_d
    addb $7, %al
.dh_d:
    addb $'0', %al
    movw $0xE9, %dx
    outb %al, %dx
    ret
