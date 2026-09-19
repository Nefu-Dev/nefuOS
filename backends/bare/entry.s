# nefuOS 内核入口 — 链接基址 0x100000（GNU as AT&T 语法）
# 双启动路径：
#   A) 传统 ISO（El Torito，BIOS）：boot.s 加载压缩 payload，stub 解压内核到 0x100000 后 ljmp，
#      落到本文件偏移 0 的 `jmp kernel_start`；bootinfo 已由 boot.s 写入 0x7000。
#   B) UEFI ESP + GRUB：GRUB 按 multiboot2 协议把本文件加载到 0x100000，进入内核时
#      EAX=0x36D76289（multiboot2 magic）、EBX=multiboot2 信息结构物理地址；
#      本代码解析 framebuffer 标签，把 LFB/宽/高/pitch/bpp 写入 0x7000 同一布局。
# 32 位保护模式进入：建页表 -> PAE -> 长模式 -> 64 位 -> 调用 nefuos_kernel_main
# 页表布局：
#   0x9000  PML4    0xA000  PDPT
#   0xB000  PD0     0xD000  PD1     0xF000  PD2     0x11000 PD3
# GDT64 位于 0x8F00；栈 0x1F000；堆 0x400000 起
.section .text
.code32

# ---- 传统路径入口（stub 解压后 ljmp $0x18, $0x100000 落到这里）----
.globl _legacy_entry
_legacy_entry:
    jmp kernel_start          # 跳过 multiboot2 头（EB rel8，2 字节）
    .org 0x8                  # 填充至 8 字节对齐：multiboot2 头位于 0x100008

# ---- Multiboot2 头（GRUB 在镜像前 32KB 内扫描，须 8 字节对齐）----
multiboot2_header:
    .long 0xE85250D6                    # magic
    .long 0                             # architecture: i386（32 位保护模式进入）
    .long mh_end - multiboot2_header    # header_length
    .long -(0xE85250D6 + (mh_end - multiboot2_header))   # checksum
    # 请求 GRUB 用 GOP 设置 1024x768x32 线性帧缓冲，并以 framebuffer 标签回传
    .align 8
    .short 5, 0                         # type=5 framebuffer, flags=0
    .long 20
    .long 1024, 768, 32                 # width, height, depth
    # 地址标签：加载到 0x100000；load_end/bss_end 由构建脚本补丁（GRUB 负责清 .bss）
    .align 8
    .short 2, 0                         # type=2 address, flags=0
    .long 24
    .long multiboot2_header             # header_addr（ld 解析为绝对地址）
    .long 0x100000                       # load_addr
    .long 0                             # load_end_addr  <- 构建时补丁
    .long 0                             # bss_end_addr   <- 构建时补丁
    # 入口地址标签：从 kernel_start 进入
    .align 8
    .short 3, 0                         # type=3 entry address, flags=0
    .long 12
    .long kernel_start
    # 结束标签
    .align 8
    .short 0, 0                         # type=0 end
    .long 8
mh_end:
    .align 8
kernel_boot_params:                     # 构建脚本补丁：.bss 起址/大小（两条路径共用，0x100000 基址）
    .long 0                             # bss_start（绝对地址）
    .long 0                             # bss_size（字节）

.globl kernel_start
kernel_start:
    cli
    cmpl $0x36D76289, %eax              # multiboot2 bootloader magic？
    je grub_mb2_path
    # ---- 传统路径：boot.s 已设置段寄存器（0x10）与 bootinfo(0x7000) ----
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    jmp common_init

grub_mb2_path:
    # ---- GRUB 路径：EBX = multiboot2 信息结构物理地址 ----
    # 先把信息结构整体拷到 0x6000（原 VBE 区，本路径未用），避免解析时
    # 对 0x7000 的 bootinfo 写入与信息结构重叠。
    movl %ebx, %esi
    movl (%esi), %ecx                   # total_size
    cmpl $0x2000, %ecx
    jbe .mb2_sz_ok
    movl $0x2000, %ecx
.mb2_sz_ok:
    movl $0x6000, %edi
    cld
    rep movsb
    # ---- 遍历标签，找 type=8 framebuffer ----
    movl $0x6008, %esi                  # 跳过信息头（u32 size + u32 reserved）
.mb2_tag_loop:
    movl (%esi), %eax                   # type
    testl %eax, %eax
    jz .mb2_no_fb
    cmpl $8, %eax
    je .mb2_fb
    movl 4(%esi), %ecx                  # 跳到下一个标签（8 字节对齐）
    addl %ecx, %esi
    addl $7, %esi
    andl $~7, %esi
    jmp .mb2_tag_loop
.mb2_fb:
    # framebuffer 标签布局：+8 addr(lo32) +16 pitch +20 width +24 height +28 bpp
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
    # GRUB 未提供 framebuffer（请求模式不可用等）：打探针 F 后停机，便于排查
    movw $0xE9, %dx
    movb $'F', %al
    outb %al, %dx
.halt_no_fb:
    cli
    hlt
    jmp .halt_no_fb
.mb2_fb_done:
    # 探针 g：GRUB bootinfo 就绪（LFB/宽/高/pitch/bpp 已写入 0x7000）
    movw $0xE9, %dx
    movb $'g', %al
    outb %al, %dx

common_init:
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
    # debugcon 探针：'B' = bss cleared
    movw $0xE9, %dx
    movb $'B', %al
    outb %al, %dx
    movq $0x7000, %rcx           # boot info（mingw x64 ABI：首参 RCX）
    # debugcon 探针：'J' = about to call kernel_main
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

# 大栈帧探测桩
.globl ___chkstk_ms
___chkstk_ms:
    ret
