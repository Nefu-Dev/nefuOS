# nefuOS kernel entry — link base 0x100000 (GNU as AT&T syntax)
# dual boot paths:
#   A) legacy ISO (El Torito, BIOS): boot.s loads the compressed payload, the stub decompresses the kernel to 0x100000 and ljmps,
#      landing on `jmp kernel_start` at offset 0 of this file; bootinfo was written by boot.s to 0x7000.
#   B) UEFI ESP + GRUB: GRUB loads this file to 0x100000 per the multiboot2 protocol; on entry
#      EAX=0x36D76289 (multiboot2 magic), EBX = physical address of the multiboot2 info structure;
#      this code parses the framebuffer tag and writes LFB/width/height/pitch/bpp to the same 0x7000 layout.
# 32-bit protected-mode entry: build page tables -> PAE -> long mode -> 64-bit -> call nefuos_kernel_main
# page table layout:
#   0x9000  PML4    0xA000  PDPT
#   0xB000  PD0     0xD000  PD1     0xF000  PD2     0x11000 PD3
# GDT64 at 0x8F00; stack at 0x1F000; heap starts at 0x400000
.section .text
.code32

# ---- legacy path entry (after stub decompress, ljmp $0x18, $0x100000 lands here) ----
.globl _legacy_entry
_legacy_entry:
    jmp kernel_start          # skip the multiboot2 header (EB rel8, 2 bytes)
    .org 0x8                  # 填充至 8 字节对齐：multiboot2 头位于 0x100008

# ---- Multiboot2 header (GRUB scans within the first 32KB of the image; must be 8-byte aligned) ----
multiboot2_header:
    .long 0xE85250D6                    # magic
    .long 0                             # architecture: i386 (32-bit protected-mode entry)
    .long mh_end - multiboot2_header    # header_length
    .long -(0xE85250D6 + (mh_end - multiboot2_header))   # checksum
    # ask GRUB to set up a 1024x768x32 linear framebuffer via GOP and return it in the framebuffer tag
    .align 8
    .short 5, 0                         # type=5 framebuffer, flags=0
    .long 20
    .long 1024, 768, 32                 # width, height, depth
    # address tag: load to 0x100000; load_end/bss_end patched by the build script (GRUB clears .bss)
    .align 8
    .short 2, 0                         # type=2 address, flags=0
    .long 24
    .long multiboot2_header             # header_addr (ld resolves to an absolute address)
    .long 0x100000                       # load_addr
    .long 0                             # load_end_addr  <- patched at build time
    .long 0                             # bss_end_addr   <- patched at build time
    # entry address tag: enter at kernel_start
    .align 8
    .short 3, 0                         # type=3 entry address, flags=0
    .long 12
    .long kernel_start
    # end tag
    .align 8
    .short 0, 0                         # type=0 end
    .long 8
mh_end:
    .align 8
kernel_boot_params:                     # build-script patch: .bss start/size (shared by both paths, 0x100000 base)
    .long 0                             # bss_start (absolute address)
    .long 0                             # bss_size (bytes)

.globl kernel_start
kernel_start:
    cli
    cmpl $0x36D76289, %eax              # multiboot2 bootloader magic?
    je grub_mb2_path
    # ---- legacy path: boot.s already set the segment registers (0x10) and bootinfo (0x7000) ----
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    jmp common_init

grub_mb2_path:
    # ---- GRUB path: EBX = physical address of the multiboot2 info structure ----
    # copy the whole info structure to 0x6000 (the unused VBE area on this path) so that
    # the bootinfo writes to 0x7000 do not overlap the info structure.
    movl %ebx, %esi
    movl (%esi), %ecx                   # total_size
    cmpl $0x2000, %ecx
    jbe .mb2_sz_ok
    movl $0x2000, %ecx
.mb2_sz_ok:
    movl $0x6000, %edi
    cld
    rep movsb
    # ---- walk the tags, look for a type=8 framebuffer ----
    movl $0x6008, %esi                  # skip the info header (u32 size + u32 reserved)
.mb2_tag_loop:
    movl (%esi), %eax                   # type
    testl %eax, %eax
    jz .mb2_no_fb
    cmpl $8, %eax
    je .mb2_fb
    movl 4(%esi), %ecx                  # jump to the next tag (8-byte aligned)
    addl %ecx, %esi
    addl $7, %esi
    andl $~7, %esi
    jmp .mb2_tag_loop
.mb2_fb:
    # framebuffer tag layout: +8 addr(lo32) +16 pitch +20 width +24 height +28 bpp
    movl 8(%esi), %eax
    movl %eax, 0x7000                   # LFB base
    movl 20(%esi), %eax
    movl %eax, 0x7004                   # width
    movl 24(%esi), %eax
    movl %eax, 0x7008                   # height
    movl 16(%esi), %eax
    movl %eax, 0x700C                   # pitch
    movl 28(%esi), %eax
    movb %al, 0x7010                    # bpp
    jmp .mb2_fb_done
.mb2_no_fb:
    # GRUB provided no framebuffer (requested mode unavailable, etc.): emit probe F then halt for debugging
    movw $0xE9, %dx
    movb $'F', %al
    outb %al, %dx
.halt_no_fb:
    cli
    hlt
    jmp .halt_no_fb
.mb2_fb_done:
    # probe g: GRUB bootinfo ready (LFB/width/height/pitch/bpp written to 0x7000)
    movw $0xE9, %dx
    movb $'g', %al
    outb %al, %dx

common_init:
    # probe e: segment setup done
    movw $0xE9, %dx
    movb $'e', %al
    outb %al, %dx
    # ---- zero the page-table area 0x9000..0x13000 ----
    movl $0x9000, %edi
    movl $((0x13000 - 0x9000) / 4), %ecx
    xorl %eax, %eax
    rep stosl
    # probe f: page-table area cleared
    movw $0xE9, %dx
    movb $'f', %al
    outb %al, %dx

    # ---- PML4[0] = 0xA000 | P,RW ----
    movl $0xA003, 0x9000
    # ---- PDPT[0..3] ----
    movl $0xB003, 0xA000
    movl $0xD003, 0xA008
    movl $0xF003, 0xA010
    movl $0x11003, 0xA018

    # ---- PD: 0..128MB WB (0x83), rest UC (0x13), 2MB large pages, mapping 0..4GB ----
    xorl %ecx, %ecx
.pd_loop:
    cmpl $2048, %ecx
    jge .pd_done
    movl %ecx, %eax
    shll $21, %eax               # phys = idx * 2MB
    orl $0x83, %eax
    cmpl $64, %ecx
    jb .pd_store
    andl $~0x83, %eax
    orl $0x9B, %eax              # UC 2MB large page P+RW+PS+CD+WT (includes the LFB region)
.pd_store:
    movl %ecx, %edx
    shrl $9, %edx
    shll $13, %edx               # (idx/512) * 0x2000
    addl $0xB000, %edx
    movl %ecx, %esi
    andl $511, %esi
    shll $3, %esi                # (idx%512) * 8
    movl %eax, (%edx,%esi)
    incl %ecx
    jmp .pd_loop
.pd_done:

    # ---- remap the LFB 2MB block to 4KB pages ----
    # TCG cannot treat PCI MMIO as a 2MB large page; a large page there
    # is cached as plain RAM and writes never reach the VGA card, which
    # shows up as garbage stripes in the emulated display.  The boot
    # sector stored the VBE physical LFB at 0x7000.  Map LFB..LFB+4MB
    # (two 2MB blocks) through 4KB page tables at 0x13000/0x14000 (+
    # PD*0x1000 so a LFB above 3GB uses tables 3/4 -> 0x16000/0x17000).
    movl 0x7000, %eax
    shrl $21, %eax               # pd index (0..2047)
    movl %eax, %ebx              # save pd index
    movl %eax, %ecx
    shrl $9, %ecx                # pd table 0..3
    shll $13, %ecx
    addl $0xB000, %ecx           # PD base for this table
    movl %eax, %esi
    andl $511, %esi
    shll $3, %esi                # entry offset in PD
    # PT base for block 1
    movl %ebx, %edx
    shrl $9, %edx
    shll $12, %edx
    addl $0x13000, %edx          # PT1
    # fill PT1: 512 x 4KB pages, phys = (LFB & ~2MB) + i*4K, UC
    movl 0x7000, %esi
    andl $0xFFE00000, %esi
    movl $512, %edi
.pt4_fill:
    movl %esi, %eax
    orl $0x13, %eax              # P+RW+CD+WT (uncached, device memory)
    movl %eax, (%edx)
    addl $4096, %esi
    addl $8, %edx
    decl %edi
    jnz .pt4_fill
    # PD entry 1 -> PT1 | P,RW
    movl %ebx, %eax
    shrl $9, %eax
    shll $12, %eax
    addl $0x13000, %eax          # PT1 base (0x13000 + table*0x1000)
    orl $3, %eax
    movl %ebx, %edi
    andl $511, %edi
    shll $3, %edi
    movl %eax, (%ecx,%edi)
    # second 2MB block (covers LFB+2MB .. LFB+4MB for big framebuffers)
    movl %ebx, %edx
    shrl $9, %edx
    shll $12, %edx
    addl $0x13000, %edx
    addl $0x1000, %edx           # PT2 = PT1 + 0x1000
    movl 0x7000, %esi
    andl $0xFFE00000, %esi
    addl $0x200000, %esi
    movl $512, %edi
.pt4_fill2:
    movl %esi, %eax
    orl $0x13, %eax
    movl %eax, (%edx)
    addl $4096, %esi
    addl $8, %edx
    decl %edi
    jnz .pt4_fill2
    movl %ebx, %eax
    shrl $9, %eax
    shll $12, %eax
    addl $0x13000, %eax          # PT1 base
    addl $0x1000, %eax           # PT2 = PT1 + 0x1000
    orl $3, %eax
    movl %ebx, %edi
    andl $511, %edi
    shll $3, %edi
    addl $8, %edi
    movl %eax, (%ecx,%edi)

    # ---- GDT64 ----
    lgdt gdt64_ptr
    # probe a: LGDT done
    movw $0xE9, %dx
    movb $'a', %al
    outb %al, %dx

    # ---- long mode ----
    movl %cr4, %eax
    orl $0x20, %eax              # PAE
    movl %eax, %cr4
    movl $0x9000, %eax
    movl %eax, %cr3
    # probe b: PAE+CR3 done
    movw $0xE9, %dx
    movb $'b', %al
    outb %al, %dx
    movl $0xC0000080, %ecx       # EFER
    rdmsr
    orl $0x100, %eax             # LME
    wrmsr
    # probe c: LME done
    movw $0xE9, %dx
    movb $'c', %al
    outb %al, %dx
    movl %cr0, %eax
    orl $0x80000001, %eax        # PG | PE
    movl %eax, %cr0
    # probe d: PG enabled
    movw $0xE9, %dx
    movb $'d', %al
    outb %al, %dx
    ljmp $0x08, $.code64

.code64
.code64:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    movq $0x1F000, %rsp
    # debugcon probe: 'L' = long mode entered
    movw $0xE9, %dx
    movb $'L', %al
    outb %al, %dx
    # ---- clear .bss (kernel.bin carries no zero padding) ----
    # Read bss_start/bss_size from kernel_boot_params (in-kernel, shared by
    # both boot paths), patched at build time from the PE section table.  The
    # __bss_start linker symbol is unusable: mingw ld emits PE symbol values
    # as RVAs (0 for .bss), which would zero-fill low memory and destroy the
    # page tables at 0x9000-0x13000.  (GRUB additionally zeroes the bss per
    # the multiboot2 Address tag; re-zeroing here is idempotent.)
    leaq kernel_boot_params(%rip), %rax
    movl (%rax), %edi           # bss_start (absolute runtime address)
    movl 4(%rax), %ecx          # bss_size (bytes)
    shrq $3, %rcx               # qword count
    xorl %eax, %eax
    rep stosq
    # debugcon probe: 'B' = bss cleared
    movw $0xE9, %dx
    movb $'B', %al
    outb %al, %dx
    movq $0x7000, %rcx           # boot info (mingw x64 ABI: first arg in RCX)
    # debugcon probe: 'J' = about to call kernel_main
    movw $0xE9, %dx
    movb $'J', %al
    outb %al, %dx
    call nefuos_kernel_main
.halt:
    cli
    hlt
    jmp .halt

# ---- GDT64 ----
.align 8
gdt64:
    .quad 0x0000000000000000            # 0x00 null
    .quad 0x00AF9A000000FFFF            # 0x08 code64 (L=1)
    .quad 0x00CF92000000FFFF            # 0x10 data64
gdt64_end:
gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .quad gdt64

# large stack-frame probe stub
.globl ___chkstk_ms
___chkstk_ms:
    ret
