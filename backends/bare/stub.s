# nefuOS boot stub — 32-bit protected-mode decompressor entry.
# Root cause fixed: kernel.bin at 1.14MB cannot be loaded by the 16-bit
# real-mode loader (physical 0xA0000-0xBFFFF is VGA/ROM hole; 16-bit segment
# math wraps past 0xFFFF beyond ~896KB; real-mode addressing caps at
# 0x10FFEF ~1.06MB).  Fix: boot.s loads a COMPRESSED payload (this stub +
# gzip raw-DEFLATE kernel) to 0x20000 (<= ~507KB, fits hole-free low RAM),
# then this stub inflates the kernel to 0x100000+ and far-jumps to it.
#
# Payload layout (built by tools/build_iso.ps1):
#   +0x00  this stub (linked flat, image base 0)
#   +0x80  kernel_src_off (u32)  <- offset of compressed kernel in payload
#   +0x84  kernel_src_len (u32)  <- compressed kernel size (bytes)
#   +0x88  kernel_dst_cap (u32)  <- decompressed capacity (bytes)
#   +len(stub.bin)  compressed kernel (raw DEFLATE, wbits=-15)
# Stub is loaded at physical 0x20000; data slots live at 0x20080/84/88.
#
# Runs in 32-bit protected mode: boot.s already set ds/es/ss/fs/gs = 0x10
# (flat data), esp = 0x1F000, A20 on, interrupts off.  cdecl call into
# inflate (no libc), then far jump to the decompressed kernel entry at
# 0x100000 (entry.s, code32; GDT 0x18 is a flat code32 segment).
.code32
.section .text
.org 0
.globl stub_start
stub_start:
    # Move stack ABOVE the decompression output window.
    # boot.s set esp=0x1F000, but inflate writes the kernel to 0x100000..0x220200
    # (kernel.bin = 1,180,160 = 0x120200 with minijs + FHS usr seed), so output
    # reaches 0x1F000 and would clobber the boot stack mid-inflate.  Put esp at
    # 0x280000 (kernel end 0x220200 + slack, still well inside 128MB RAM).
    movl $0x280000, %esp
    # debugcon probe 'S': stub entered (32-bit PM, payload loaded)
    movw $0xE9, %dx
    movb $'S', %al
    outb %al, %dx
    # cdecl: nefu_inflate(dst=0x100000, dst_cap, src=0x20000+src_off, src_len)
    # args pushed right-to-left: src_len, src, dst_cap, dst
    movl 0x20084, %eax          # src_len
    pushl %eax
    movl 0x20080, %eax          # src_off
    addl $0x20000, %eax         # src = payload base + off
    pushl %eax
    movl 0x20088, %eax          # dst_cap
    pushl %eax
    pushl $0x100000             # dst
    call _nefu_inflate
    addl $16, %esp
    movl %eax, %ebx             # save result for error reporting
    testl %eax, %eax
    jle stub_fail
    # probe 'G': decompression succeeded (eax = byte count)
    movw $0xE9, %dx
    movb $'G', %al
    outb %al, %dx
    ljmp $0x18, $0x100000       # flat code32 -> kernel entry (entry.s)
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

# ---- build-time patched slots (0x80/0x84/0x88 in stub.bin) ----
.org 0x80
kernel_src_off: .long 0
kernel_src_len: .long 0
kernel_dst_cap: .long 0
