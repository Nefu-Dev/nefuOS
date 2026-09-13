# nefuOS 内核入口 — 链接基址 0x20000（GNU as AT&T 语法）
# 32 位保护模式进入：建页表 -> PAE -> 长模式 -> 64 位 -> 调用 nefuos_kernel_main
# 页表布局：
#   0x9000  PML4    0xA000  PDPT
#   0xB000  PD0     0xD000  PD1     0xF000  PD2     0x11000 PD3
# GDT64 位于 0x8F00；栈 0x1F000；堆 0x400000 起
.section .text
.code32
.globl kernel_start
kernel_start:
    cli
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    # 探针 e：段设置完成
    movw $0xE9, %dx
    movb $'e', %al
    outb %al, %dx
    # ---- 清零页表区 0x9000..0x13000 ----
    movl $0x9000, %edi
    movl $((0x13000 - 0x9000) / 4), %ecx
    xorl %eax, %eax
    rep stosl
    # 探针 f：页表区清零完成
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

    # ---- PD：0..128MB WB(0x83)，其余 UC(0x13)，2MB 大页，映射 0..4GB ----
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
    orl $0x9B, %eax              # UC 2MB 大页 P+RW+PS+CD+WT（含 LFB 区）
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
    # 探针 a：LGDT 完成
    movw $0xE9, %dx
    movb $'a', %al
    outb %al, %dx

    # ---- 长模式 ----
    movl %cr4, %eax
    orl $0x20, %eax              # PAE
    movl %eax, %cr4
    movl $0x9000, %eax
    movl %eax, %cr3
    # 探针 b：PAE+CR3 完成
    movw $0xE9, %dx
    movb $'b', %al
    outb %al, %dx
    movl $0xC0000080, %ecx       # EFER
    rdmsr
    orl $0x100, %eax             # LME
    wrmsr
    # 探针 c：LME 完成
    movw $0xE9, %dx
    movb $'c', %al
    outb %al, %dx
    movl %cr0, %eax
    orl $0x80000001, %eax        # PG | PE
    movl %eax, %cr0
    # 探针 d：PG 开启
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
    # debugcon 探针：'L' = 长模式已进入
    movw $0xE9, %dx
    movb $'L', %al
    outb %al, %dx
    # ---- clear .bss (kernel.bin carries no zero padding) ----
    # Read bss_start/bss_size from the boot sector slots (0x7C00+0x1C0),
    # patched at build time from the PE section table.  The __bss_start
    # linker symbol is unusable: mingw ld emits PE symbol values as RVAs
    # (0 for .bss), which would zero-fill low memory and destroy the
    # page tables at 0x9000-0x13000.
    movq $0x7C00, %rax
    movl 0x1C0(%rax), %edi      # bss_start (absolute runtime address)
    movl 0x1C4(%rax), %ecx      # bss_size (bytes)
    shrq $3, %rcx               # qword count
    xorl %eax, %eax
    rep stosq
    # debugcon 探针：'B' = bss cleared
    movw $0xE9, %dx
    movb $'B', %al
    outb %al, %dx
    movq $0x7000, %rcx           # boot info（mingw x64 ABI：首参 RCX）
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

# 大栈帧探测桩
.globl ___chkstk_ms
___chkstk_ms:
    ret