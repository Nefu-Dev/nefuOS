// nefuOS Linux static-ELF runtime (elfrun).
//
// Loads a statically-linked x86_64 ELF64, maps its PT_LOAD segments into a
// guest memory image, builds a minimal initial stack, and (on the bare-metal
// backend) executes it, emulating a small set of Linux syscalls.  The
// syscall ABI + loader are platform-independent and host-testable; the ring-0
// execution path is guarded by NEFU_BARE.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace elf {

// ---- ELF64 program header / segment model ----
struct Seg {
    uint64_t vaddr;
    uint64_t memsz;
    const uint8_t* fdata;   // pointer into the raw ELF bytes
    uint64_t fsize;
    uint32_t flags;         // PF_R=1 PF_W=2 PF_X=4
};

struct Image {
    const uint8_t* raw;
    uint32_t size;
    uint64_t entry;
    Seg segs[8];
    int nsegs;
    uint64_t load_lo;       // lowest vaddr
    uint64_t load_hi;       // highest vaddr + memsz
};

// Parse an ELF64 (EM_X86_64, ET_EXEC) header + program headers.
// Returns 0 ok, else negative (see codes below).
int  parse(const uint8_t* data, uint32_t size, Image& out);
enum { E_OK = 0, E_NOT_ELF = -1, E_NOT_64 = -2, E_NOT_X86 = -3,
       E_TRUNC = -4, E_TOO_MANY_SEGS = -5, E_NO_LOAD = -6 };

// ---- guest memory image (host-testable) ----
struct Guest {
    uint8_t* mem;           // kalloc'd guest image (size bytes)
    uint64_t base;          // absolute vaddr where mem is placed on bare
    uint64_t size;
    uint64_t entry;         // absolute entry vaddr
    uint64_t rsp;           // initial stack pointer (absolute)
    uint64_t brk;           // initial program break (absolute)
    uint64_t brk_limit;
    char     argv0[64];
};

// Map the segments of `img` into a fresh guest image at its own vaddrs and
// build a minimal Linux initial stack (argc/argv/envp/auxv).  Returns 0 ok.
int  build_guest(const Image& img, const char* argv0, Guest& out);

// ---- Linux x86_64 syscall ABI (minimal emulation) ----
struct SysCtx {
    // route write() on fd 1/2 (and printf-ish helpers) to the OS output.
    int  (*write_fn)(const void* buf, int64_t n, void* ud);
    void* ud;
    int64_t exit_code;      // set by exit()/exit_group()
    uint64_t brk_cur;       // current program break (absolute)
    uint64_t brk_limit;
    uint64_t mmap_cur;      // mmap allocation cursor
    uint64_t mmap_limit;
    // embedded X11 server connection (guest Xlib client).  -1 = not open.
    int  x_fd;
    // syscall trace: when non-zero, every emulated syscall prints
    // "nr=a0,a1,a2 -> ret" to the COM1 UART (boot-time debugging only).
    int  trace;
    // syscall budget: when > 0, the guest is force-terminated (as if exit(0))
    // once this many syscalls have been emulated.  0 = unlimited.  Prevents a
    // busy-waiting X client from freezing the single-threaded kernel.
    uint64_t syscall_budget;
};

int64_t syscall(SysCtx& ctx, int64_t nr, int64_t a0, int64_t a1, int64_t a2,
                int64_t a3, int64_t a4, int64_t a5);

// ---- execution (bare-metal only; host has no CPU to run x86 ELF) ----
// Loads the guest image to its absolute vaddrs, hooks the #UD handler for
// `syscall` (0f 05), and jumps to the entry point.  Returns the exit code on
// return, or -1 when this backend cannot execute ELF.
int  execute(Guest& g, SysCtx& ctx);

} // namespace elf
} // namespace nefu
