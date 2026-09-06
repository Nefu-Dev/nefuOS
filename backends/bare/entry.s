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