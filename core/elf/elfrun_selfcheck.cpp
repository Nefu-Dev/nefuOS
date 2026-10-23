// core/elf/elfrun_selfcheck.cpp
//
// Boot-time check of the Linux static-ELF runtime on the bare-metal backend,
// wired to the REAL VFS/runelf path: the ELF is seeded into the VFS as a file
// (the same place a `.nefud` install drops its ELF), then resolved and run
// through the same parse -> build_guest -> execute chain `runelf` uses.
// Output goes to COM1 so a headless QEMU run can be verified with `-serial file:`.
//
// NEFU_BARE only: the host backend has no CPU to run x86 ELF.
#include "elfrun.h"
#include "elfrun_elfbin.h"     // NEFU_ELF_SELFCHECK_BIN: seed bytes for the VFS file
#include "../vfs/vfs.h"        // g_vfs / FSNode (VFS tree exists at call time)
#include "../klib/klib.h"
#include "../platform.h"
#include <stdint.h>
#include <stddef.h>

#ifdef NEFU_BARE

namespace nefu {
namespace elf {

static inline uint8_t sc_inb(uint16_t port){ uint8_t v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline void sc_outb(uint16_t port, uint8_t v){ __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port)); }
static inline void sc_putc(char c) {
    while (!(sc_inb(0x3F8 + 5) & 0x20)) {}
    sc_outb(0x3F8, (uint8_t)c);
}
static void sc_puts(const char* s) { while (*s) sc_putc(*s++); }

static void sc_puthex(uint64_t v) {
    static const char hx[] = "0123456789abcdef";
    char b[20]; int n = 0;
    if (!v) { sc_putc('0'); return; }
    while (v) { b[n++] = hx[v & 15]; v >>= 4; }
    while (n) sc_putc(b[--n]);
}

// Linux write() on fd 1/2 -> UART (same sink the GUI `runelf` uses via the
// terminal, but straight to COM1 so a headless QEMU boot can observe it).
static int sc_write(const void* buf, int64_t n, void* ud) {
    (void)ud;
    const char* p = (const char*)buf;
    for (int64_t i = 0; i < n; i++) sc_putc(p[i]);
    return (int)n;
}

// Seed /usr/share/apps/hello.elf in the VFS if it is not already there
// (simulating a `.nefud` install dropping its ELF into the tree).  Uses the
// same embedded bytes that used to be run directly, now materialised as a file.
static FSNode* ensure_hello_elf() {
    const char* path = "/usr/share/apps/hello.elf";
    FSNode* f = g_vfs->resolve(path);
    if (f && !f->is_dir && f->size == NEFU_ELF_SELFCHECK_LEN) return f;
    if (!f || f->is_dir) {
        g_vfs->mkdir("/usr/share");
        g_vfs->mkdir("/usr/share/apps");
        f = g_vfs->create_file(path);
    }
    if (!f) return 0;
    if (!g_vfs->write_file(f, NEFU_ELF_SELFCHECK_BIN, NEFU_ELF_SELFCHECK_LEN)) return 0;
    return f;
}

extern "C" void nefu_elf_selfcheck(void) {
    sc_puts("\r\n[ELFREQ] runelf(vfs) begin\r\n");
    FSNode* f = ensure_hello_elf();
    if (!f) { sc_puts("[ELFREQ] vfs seed FAIL\r\n"); return; }
    sc_puts("[ELFREQ] vfs: /usr/share/apps/hello.elf ");
    sc_puthex(f->size); sc_puts(" B\r\n");

    Image img; memset(&img, 0, sizeof(img));
    int e = parse(f->data, f->size, img);
    if (e != E_OK) {
        sc_puts("[ELFREQ] parse FAIL rc="); sc_puthex((uint64_t)(int64_t)e); sc_puts("\r\n"); return;
    }
    Guest g; memset(&g, 0, sizeof(g));
    int be = build_guest(img, "hello-linux-elf", g);
    if (be != 0) {
        sc_puts("[ELFREQ] build_guest FAIL rc="); sc_puthex((uint64_t)(int64_t)be); sc_puts("\r\n"); return;
    }
    SysCtx ctx; memset(&ctx, 0, sizeof(ctx));
    ctx.x_fd = -1;                       // no X connection for the demo ELF
    ctx.write_fn = sc_write;
    ctx.brk_cur = g.brk; ctx.brk_limit = g.brk_limit;
    ctx.mmap_cur = g.brk; ctx.mmap_limit = g.brk_limit;
    sc_puts("[ELFREQ] run entry=0x"); sc_puthex(g.entry);
    sc_puts(" base=0x"); sc_puthex(g.base); sc_puts("\r\n");
    // Jumps into the guest: its `write` syscall emits
    // "Hello from Linux ELF in nefuOS!" on the UART.  The demo ELF never
    // returns to the kernel, so the line after this is unreachable.
    execute(g, ctx);
    sc_puts("[ELFREQ] execute returned (unexpected)\r\n");
}

} // namespace elf
} // namespace nefu
#endif // NEFU_BARE
