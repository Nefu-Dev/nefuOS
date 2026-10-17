# setjmp.S — freestanding x86-64 setjmp/longjmp for the nefuOS kernel.
# jmp_buf layout (8 x 8 bytes): rbx, rbp, r12, r13, r14, r15, rsp, rip
# SysV AMD64 calling convention: arg0 = jmp_buf* in rdi, arg1 (longjmp val) in rsi.
# MINGW COFF target: C symbols have no leading underscore; no .type directives.

    .text
    .globl setjmp
setjmp:
    movq %rbx, 0(%rdi)
    movq %rbp, 8(%rdi)
    movq %r12, 16(%rdi)
    movq %r13, 24(%rdi)
    movq %r14, 32(%rdi)
    movq %r15, 40(%rdi)
    leaq 8(%rsp), %rax            # caller's stack pointer (after return address)
    movq %rax, 48(%rdi)
    movq (%rsp), %rax             # return address (RIP)
    movq %rax, 56(%rdi)
    xorl %eax, %eax               # return 0
    ret

    .globl longjmp
longjmp:
    movq 0(%rdi), %rbx
    movq 8(%rdi), %rbp
    movq 16(%rdi), %r12
    movq 24(%rdi), %r13
    movq 32(%rdi), %r14
    movq 40(%rdi), %r15
    movq 48(%rdi), %rsp
    movl %esi, %eax               # return value = val
    jmp *56(%rdi)
