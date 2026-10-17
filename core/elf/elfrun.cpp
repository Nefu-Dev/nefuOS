// nefuOS Linux static-ELF runtime (elfrun).
#include "elfrun.h"
#include "../klib/klib.h"      // memcpy/memset/strlen/strncpy/strcpy (nefu::)
#include "../platform.h"        // kalloc/kfree
#include "../x11/xserver.h"     // embedded X11 server (guest Xlib clients)

namespace nefu { namespace elf {

static uint16_t rd16(const uint8_t* p, int o){ return (uint16_t)(p[o] | (p[o+1]<<8)); }
static uint32_t rd32(const uint8_t* p, int o){ return (uint32_t)(p[o] | (p[o+1]<<8) | (p[o+2]<<16) | (p[o+3]<<24)); }
static uint64_t rd64(const uint8_t* p, int o){ uint64_t v=0; for(int i=0;i<8;i++) v |= (uint64_t)p[o+i]<<(8*i); return v; }

int parse(const uint8_t* d, uint32_t size, Image& out) {
    out.raw = d; out.size = size; out.nsegs = 0; out.entry = 0;
    out.load_lo = 0; out.load_hi = 0;
    if (size < 64) return E_TRUNC;
    if (!(d[0]==0x7f && d[1]=='E' && d[2]=='L' && d[3]=='F')) return E_NOT_ELF;
    if (d[4] != 2 || d[5] != 1) return E_NOT_64;          // ELFCLASS64, ELFDATA2LSB
    if (rd16(d,18) != 62) return E_NOT_X86;               // EM_X86_64
    uint32_t e_phoff = rd32(d,32);
    uint16_t e_phentsize = rd16(d,52), e_phnum = rd16(d,54);
    out.entry = rd64(d,24);
    if (e_phoff + (uint32_t)e_phentsize * e_phnum > size) return E_TRUNC;
    for (int i=0;i<e_phnum;i++){
        const uint8_t* p = d + e_phoff + (uint32_t)i*e_phentsize;
        if (rd32(p,0) != 1) continue;                     // PT_LOAD
        if (out.nsegs >= 8) return E_TOO_MANY_SEGS;
        Seg& s = out.segs[out.nsegs];
        s.flags = rd32(p,4);
        s.vaddr = rd64(p,16);
        uint64_t p_offset = rd64(p,8);
        s.fsize = rd64(p,32); s.memsz = rd64(p,40);
        if (p_offset + s.fsize > size) return E_TRUNC;
        s.fdata = d + p_offset;
        out.nsegs++;
        if (out.nsegs==1 || s.vaddr < out.load_lo) out.load_lo = s.vaddr;
        if (s.vaddr + s.memsz > out.load_hi) out.load_hi = s.vaddr + s.memsz;
    }
    if (out.nsegs == 0) return E_NO_LOAD;
    return 0;
}

int build_guest(const Image& img, const char* argv0, Guest& g) {
    const uint64_t STACK = 0x10000;                       // 64 KB stack
    const uint64_t HEAP  = 0x10000;                       // 64 KB brk/mmap region
    uint64_t need = (img.load_hi - img.load_lo + 0xFFF) & ~0xFFFULL;
    uint64_t total = need + HEAP + STACK;
    if (total > 0x2000000) return -7;                     // sanity cap: 32 MB guest
    uint8_t* mem = (uint8_t*)kalloc((size_t)total);
    if (!mem) return -8;
    memset(mem, 0, (size_t)total);
    g.mem = mem; g.base = img.load_lo; g.size = total;
    g.entry = img.entry;
    if (argv0) { strncpy(g.argv0, argv0, sizeof(g.argv0)-1); g.argv0[sizeof(g.argv0)-1]=0; }
    else g.argv0[0] = 0;
    if (!g.argv0[0]) { strcpy(g.argv0, "app"); }
    for (int i=0;i<img.nsegs;i++){
        const Seg& s = img.segs[i];
        uint64_t off = s.vaddr - g.base;
        if (off + s.fsize <= total)
            memcpy(mem + off, s.fdata, (size_t)s.fsize);
        // memsz-fsize tail stays zeroed (BSS)
    }
    // Linux initial stack (ascending address): argc, argv[0..argc], NULL,
    // envp[0..], NULL, auxv[..], AT_NULL.  argv0 string sits at the top.
    uint64_t stack_top = g.base + total;
    uint64_t rsp = stack_top;
    size_t alen = strlen(g.argv0);
    rsp -= alen + 1;
    memcpy(mem + (rsp - g.base), g.argv0, alen + 1);   // argv0 string (high addr)
    uint64_t q[16];
    int nq = 0;
    q[nq++] = 1;                            // argc
    q[nq++] = rsp;                          // argv[0]
    q[nq++] = 0;                            // argv terminator
    q[nq++] = 0;                            // envp terminator
    q[nq++] = 3; q[nq++] = img.load_lo + 64; // AT_PHDR
    q[nq++] = 4; q[nq++] = 56;              // AT_PHENT
    q[nq++] = 5; q[nq++] = img.nsegs;       // AT_PHNUM
    q[nq++] = 6; q[nq++] = 4096;            // AT_PAGESZ
    q[nq++] = 9; q[nq++] = img.entry;       // AT_ENTRY
    q[nq++] = 0; q[nq++] = 0;               // AT_NULL
    rsp -= nq * 8;
    memcpy(mem + (rsp - g.base), q, sizeof(q[0]) * (size_t)nq);
    g.rsp = rsp;
    g.brk = (img.load_hi + 0xFFF) & ~0xFFFULL;
    g.brk_limit = g.base + need + HEAP;
    return 0;
}

// ---- Linux x86_64 syscall emulation (minimal, demo-oriented) ----
int64_t syscall(SysCtx& c, int64_t nr, int64_t a0, int64_t a1, int64_t a2,
                int64_t a3, int64_t a4, int64_t a5) {
    (void)a3; (void)a4; (void)a5;
    switch (nr) {
    case 0:                                               // read
        if (a0 == c.x_fd) {
            int n = x11::xs_guest_read((int)a0, (void*)(uintptr_t)a1, (int)a2);
            return n ? n : -11;                           // EAGAIN when empty
        }
        return 0;                                         // EOF
    case 1:                                               // write
        if (a0 == 1 || a0 == 2)
            return c.write_fn ? c.write_fn((const void*)(uintptr_t)a1, a2, c.ud) : a2;
        if (a0 == c.x_fd)
            return x11::xs_guest_write((int)a0, (const void*)(uintptr_t)a1, (int)a2);
        return -1;                                        // EBADF
    case 2: case 257: return -2;                          // open/openat: ENOENT
    case 3:                                               // close
        if (a0 == c.x_fd) {
            x11::xs_guest_close((int)a0);
            c.x_fd = -1;
        }
        return 0;
    case 4: case 5: return 0;                             // stat/fstat
    case 9:                                               // mmap
        { int64_t addr = (int64_t)((c.mmap_cur + 0xFFF) & ~0xFFFULL);
          if (addr + a1 > (int64_t)c.mmap_limit) return -12;
          c.mmap_cur = (uint64_t)addr + (uint64_t)a1; return addr; }
    case 11: return 0;                                    // munmap
    case 12:                                              // brk
        if (a0 == 0) return (int64_t)c.brk_cur;
        if (a0 >= (int64_t)c.brk_limit) return (int64_t)c.brk_cur;
        c.brk_cur = (uint64_t)a0; return a0;
    case 13: return 0;                                    // rt_sigprocmask
    case 39: return 1;                                    // getpid
    case 41:                                              // socket
        { int f = x11::xs_guest_socket((int)a0, (int)a1);
          if (f >= 0) c.x_fd = f;
          return f; }
    case 42:                                              // connect
        if (a0 == c.x_fd)
            return x11::xs_guest_connect((int)a0, (const void*)(uintptr_t)a1, (int)a2);
        return -1;
    case 44:                                              // sendto
        if (a0 == c.x_fd)
            return x11::xs_guest_write((int)a0, (const void*)(uintptr_t)a1, (int)a2);
        return -1;
    case 45:                                              // recvfrom
        if (a0 == c.x_fd)
            return x11::xs_guest_read((int)a0, (void*)(uintptr_t)a1, (int)a2);
        return -1;
    case 49: case 50: return 0;                           // bind / listen
    case 52: return -1;                                   // accept (not needed)
    case 60: case 231:                                    // exit / exit_group
        c.exit_code = a0; return 0;
    case 62: case 63: case 64: case 65: return 0;         // kill/tgkill/signal
    case 102: return 1000;                                // getuid
    case 104: return 1000;                                // getgid
    case 158: return 0;                                   // arch_prctl
    case 186: return 1;                                   // gettid
    case 217: return 0;                                   // getdents64 (empty)
    case 218: return 1;                                   // set_tid_address
    default: return -38;                                  // ENOSYS
    }
}

// ---- execution (bare-metal) ----
#ifdef NEFU_BARE
namespace {
SysCtx* g_ctx = 0;
// register layout must match the asm stub's push order (rbx-relative)
struct ElfRegs { int64_t r11, r10, r9, r8, rdi, rsi, rdx, rcx, rbx, rax; };
}
// Guest-exit hand-back state, shared with the #UD asm below (extern "C" so the
// asm can reach the symbols by their unmangled names).  When a guest calls
// exit()/exit_group(), the #UD path jumps to nefu_guest_exit_trampoline, which
// restores the kernel context saved by execute() and retq's back to execute()'s
// caller.  Previously the #UD handler resumed the guest instead, so a guest
// that called exit() (like the boot hello.elf) never returned and the boot-time
// GUI paint (bare.cpp nefuos_frame) never ran.
extern "C" uint64_t           nefu_elf_gk_rsp = 0;    // caller RSP (return address at [caller RSP])
extern "C" uint64_t           nefu_elf_gk_rbp = 0;    // caller rbp
extern "C" uint64_t           nefu_elf_gk_rbx = 0, nefu_elf_gk_r12 = 0,
                              nefu_elf_gk_r13 = 0, nefu_elf_gk_r14 = 0, nefu_elf_gk_r15 = 0;
extern "C" volatile uint64_t  nefu_elf_guest_exited = 0;  // set when the guest called exit()
extern "C" int64_t            nefu_elf_exit_ret = 0;  // guest exit code returned to the kernel

extern "C" int64_t nefu_elf_ud_dispatch(ElfRegs* r) {
    if (g_ctx->trace) {
        // COM1 UART (16550) diagnostic output for boot-time syscall tracing.
        auto tr_outb = [](uint16_t port, uint8_t v) {
            __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
        };
        auto tr_inb = [](uint16_t port) -> uint8_t {
            uint8_t v;
            __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
            return v;
        };
        auto tr_puts = [&](const char* s) {
            for (; *s; s++) { while (!(tr_inb(0x3F8 + 5) & 0x20)); tr_outb(0x3F8, (uint8_t)*s); }
        };
        char b[24]; int n = 0;
        const char* h = "0123456789abcdef";
        auto px = [&](uint64_t v) { char t[20]; int m = 0; if (!v) { b[n++] = '0'; return; } while (v) { t[m++] = h[v & 15]; v >>= 4; } while (m) b[n++] = t[--m]; b[n] = 0; };
        tr_puts("[T] nr="); px((uint64_t)r->rax); tr_puts(b);
        tr_puts(" a0="); px((uint64_t)r->rdi); tr_puts(b);
        tr_puts(" a1="); px((uint64_t)r->rsi); tr_puts(b);
        tr_puts(" a2="); px((uint64_t)r->rdx); tr_puts(b);
        tr_puts("\r\n");
    }
    int64_t ret = syscall(*g_ctx, r->rax, r->rdi, r->rsi, r->rdx, r->r10, r->r8, r->r9);
    if (r->rax == 60 || r->rax == 231) {   // exit / exit_group: hand control back to the kernel
        nefu_elf_exit_ret = g_ctx->exit_code;
        nefu_elf_guest_exited = 1;
        return ret;
    }
    // syscall budget: terminate a guest that would busy-wait forever (e.g. an
    // interactive X client under the single-threaded Electron window model).
    if (g_ctx->syscall_budget && --g_ctx->syscall_budget == 0) {
        nefu_elf_exit_ret = g_ctx->exit_code = 0;
        nefu_elf_guest_exited = 1;
    }
    return ret;
}
extern "C" void nefu_ud_syscall_stub(void);
// #UD handler: on `0f 05` (syscall) emulate a Linux syscall and resume at RCX.
// Written in COFF-compatible asm (.text/.globl) because the bare kernel is
// linked as a PE32+ image (ld -mi386pep) and rebinned to flat multiboot.
__asm__(
".text\n"
".globl nefu_ud_syscall_stub\n"
"nefu_ud_syscall_stub:\n"
"  pushq %rax\n  pushq %rbx\n  pushq %rcx\n  pushq %rdx\n  pushq %rsi\n"
"  pushq %rdi\n  pushq %r8\n  pushq %r9\n  pushq %r10\n  pushq %r11\n"
"  movq %rsp, %rbx\n"
"  movq 80(%rbx), %rax\n"          // faulting RIP
"  cmpb $0x0f, (%rax)\n  jne .Lud_skip\n"
"  cmpb $0x05, 1(%rax)\n  jne .Lud_skip\n"
"  movq %rbx, %rcx\n"              // rcx = &ElfRegs (PE/COFF x64: arg0 in RCX, not RDI)
"  call nefu_elf_ud_dispatch\n"
"  movq nefu_elf_guest_exited(%rip), %rcx\n"   // did the guest call exit()?
"  testq %rcx, %rcx\n"
"  jnz .Lguest_exit\n"              // yes -> return to the kernel, do not resume the guest
"  movq %rax, 72(%rbx)\n"          // result -> saved rax
"  movq 80(%rbx), %rax\n  addq $2, %rax\n  movq %rax, 80(%rbx)\n"   // resume RIP after the 0f 05 (skip it)
"  movq 96(%rbx), %rax\n  movq %rax, 96(%rbx)\n"    // keep hardware RFLAGS (no-op)
".Lud_skip:\n"
"  movq 64(%rbx), %rcx\n"          // original RBX (scratch via RCX)
"  movq 72(%rbx), %rax\n"          // result (or original RAX)
"  movq %rcx, %rbx\n"
"  popq %r11\n  popq %r10\n  popq %r9\n  popq %r8\n  popq %rdi\n"
"  popq %rsi\n  popq %rdx\n  popq %rcx\n  popq %rbx\n  popq %rax\n"
"  iretq\n"
".Lguest_exit:\n"
"  jmp nefu_guest_exit_trampoline\n"
".globl nefu_guest_exit_trampoline\n"
"nefu_guest_exit_trampoline:\n"
"  cld\n"                                          // guest may have set DF
"  movq nefu_elf_exit_ret(%rip), %rax\n"
"  movq nefu_elf_gk_rsp(%rip), %rsp\n"
"  movq nefu_elf_gk_rbx(%rip), %rbx\n"
"  movq nefu_elf_gk_rbp(%rip), %rbp\n"
"  movq nefu_elf_gk_r12(%rip), %r12\n"
"  movq nefu_elf_gk_r13(%rip), %r13\n"
"  movq nefu_elf_gk_r14(%rip), %r14\n"
"  movq nefu_elf_gk_r15(%rip), %r15\n"
"  movq $0, nefu_elf_guest_exited(%rip)\n"
"  retq\n");

// execute() forces a frame pointer so it can hand its caller's stack context to
// the guest-exit trampoline (nefu_elf_gk_rsp = caller RSP, pointing at the return
// address).  The guest clobbers the caller's callee-saved registers, so we save
// them here and the trampoline restores them before retq'ing back to our caller.
int execute(Guest& g, SysCtx& ctx) __attribute__((optimize("no-omit-frame-pointer")));
int execute(Guest& g, SysCtx& ctx) {
    g_ctx = &ctx;
    uint64_t fbp;
    uint64_t rbp0;
    __asm__ volatile(
        "movq %%rbp, %0\n\t"          // frame pointer = caller RSP - 8
        "movq (%%rbp), %1\n\t"        // caller's saved rbp (via a register: mem->mem mov is illegal on x86)
        "movq %%rbx, %2\n\tmovq %%r12, %3\n\tmovq %%r13, %4\n\tmovq %%r14, %5\n\tmovq %%r15, %6"
        : "=r"(fbp), "=r"(rbp0), "=m"(nefu_elf_gk_rbx), "=m"(nefu_elf_gk_r12),
          "=m"(nefu_elf_gk_r13), "=m"(nefu_elf_gk_r14), "=m"(nefu_elf_gk_r15)
        : : "memory");
    nefu_elf_gk_rbp = rbp0;
    nefu_elf_gk_rsp = fbp + 8;        // caller RSP (return address lives at [caller RSP])
    nefu_elf_guest_exited = 0;
    nefu_elf_exit_ret = 0;
    // install the guest image at its absolute vaddrs (identity-mapped region)
    memcpy((void*)(uintptr_t)g.base, g.mem, (size_t)g.size);
    // jump to the ELF entry with the built initial stack; the guest's exit()
    // syscall returns here via nefu_guest_exit_trampoline.
    __asm__ volatile("mov %0, %%rsp\n\tjmp *%1\n\t"
                     :: "r"(g.rsp), "r"(g.entry) : "memory");
    return (int)nefu_elf_exit_ret;    // reached only if the guest falls back without exit()
}
#else
int execute(Guest& g, SysCtx& ctx) {
    (void)g; (void)ctx;
    return -1;                    // host backend has no CPU to run x86 ELF
}
#endif

} // namespace elf
} // namespace nefu
