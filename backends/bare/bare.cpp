/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS (x86_64, QEMU/)
// platform implementation: LFB, PIT, PS/2 +, RTC, UART debug,
#include <stdint.h>
#include <stddef.h>
#include "../../core/klib/klib.h"
#include "../../core/platform.h"
#include "../../core/gui/gfx.h"   // full Surface definition
#include "../../core/audio/audio_engine.h"   // integer audio engine (PCI sound cards)
#include "../../core/net/httpbare.h"   // bare-metal HTTP(S) client for npkg/update
#include "../../core/net/tls13.h"       // native TLS 1.3 client (socket-agnostic)
#include "../../core/netproto/websocket.h" // RFC 6455 framing helpers
#include "../../core/drivers/display.h"    // GPU driver: PCI display probe + framebuffer backends

namespace nefu {

// ===================== port I/O =====================
static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outw(uint16_t port, uint16_t v) {
    __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(port));
}
static inline void io_wait() { outb(0x80, 0); }

// ===================== UART debug =====================
static void uart_init() {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}
static void uart_putc(char c) {
    while (!(inb(0x3F8 + 5) & 0x20)) {}
    outb(0x3F8, (uint8_t)c);
}

// ===================== =====================
// Arena starts at 0x2020000: above the kernel image (linked at 0x100000,
// ends < 0xB00000, enforced at build time) and above the entry/stub stacks
// (0x2000000) and the stub staging region (0xB00000..~0x13D0000).
// Kernel arena: 512 MB budget (kernel + NVFS + network). Browser arena:
// a separate 2 GB region (BROWSER_ARENA_BASE..+2GB) so the browser can grow
// up to its own 2 GB budget without starving the kernel.
#define ARENA_BASE 0x2020000u
#define ARENA_SIZE (512u * 1024u * 1024u)      // kernel + NVFS + network
#define BROWSER_ARENA_BASE 0x40000000u
#define BROWSER_ARENA_SIZE (2048u * 1024u * 1024u)  // browser, 2 GB
struct Blk { uint32_t size; Blk* next; };   // 8 bytes, 16
// The top bit of Blk::size marks a block that is currently on a free list.
// All real sizes are 16-byte aligned and far below 2 GB, so the bit is spare.
// It replaces the old O(n) "is this block already on the free list?" walk in
// kfree() -- QuickJS GC frees tens of thousands of small objects per cycle,
// so scanning the whole list on every free made the kernel heap quadratic and
// was the main reason a 3.36 MB bundle appeared to hang under QEMU TCG.
#define BLK_FREE 0x80000000u
static inline uint32_t blk_size(Blk* b) { return b->size & ~BLK_FREE; }
static uint32_t s_arena_next = ARENA_BASE;
static Blk* s_free = 0;
// ---- browser arena (independent bump + free list) ----
static uint32_t s_bnext = BROWSER_ARENA_BASE;
static Blk* s_bfree = 0;
uint64_t browser_mem_used_bytes = 0;

static void* b_alloc_from_bump(uint32_t sz) {
    uint32_t need = (sz + 15) & ~15u;
    uint32_t cur = s_bnext;
    if (cur + need + 16 > BROWSER_ARENA_BASE + BROWSER_ARENA_SIZE) return 0;
    s_bnext = cur + need + 16;
    Blk* b = (Blk*)cur;
    b->size = need;
    return (void*)(b + 1);
}

static void* alloc_from_bump(uint32_t sz) {
    uint32_t need = (sz + 15) & ~15u;
    uint32_t cur = s_arena_next;
    if (cur + need + 16 > ARENA_BASE + ARENA_SIZE) return 0;
    s_arena_next = cur + need + 16;
    Blk* b = (Blk*)cur;
    b->size = need;
    return (void*)(b + 1);
}

// A double free pushes the same block onto s_free twice; because kfree()
// stores the next pointer *inside* the freed block, the second push makes
// b->next point at b itself. kalloc() then either hands the same bytes to two
// live owners (mid-stream data corruption) or walks that cycle forever when
// the block is too small for the request (a silent hang with no log output).
// Guard both ends so a corrupt list can never wedge the kernel.
#define KHEAP_WALK_LIMIT 200000u
void* kalloc(size_t sz) {
    if (sz == 0) sz = 8;
    uint32_t need = ((uint32_t)sz + 15) & ~15u;

    Blk** pp = &s_free;
    uint32_t guard = 0;
    while (*pp) {
        if (++guard > KHEAP_WALK_LIMIT) {
            static bool cyc_once = false;
            if (!cyc_once) {
                cyc_once = true;
                klogf("KALLOC CYCLE need=0x%x (free list corrupt)\n", (unsigned)need);
            }
            break;   // fall back to bump allocation; never spin
        }
        if (blk_size(*pp) >= need) {
            Blk* b = *pp;
            *pp = b->next;
            b->size = blk_size(b);        // clear the free marker
            return (void*)(b + 1);
        }
        pp = &(*pp)->next;
    }
    void* r = alloc_from_bump(need);
    // diagnostic: bump pointer must stay inside the arena; anything else
    // means s_arena_next (in .data) was clobbered by an out-of-bounds write
    if (s_arena_next < ARENA_BASE || s_arena_next > ARENA_BASE + ARENA_SIZE) {
        klogf("KALLOC BAD bump=0x%x need=0x%x\n", (unsigned)s_arena_next, (unsigned)need);
    }
    return r;
}

void kfree(void* p) {
    if (!p) return;
    Blk* b = (Blk*)p - 1;
    // O(1) double-free detection: the marker is cleared again by kalloc().
    // Previously this walked the entire free list, which is O(n) per free and
    // O(n^2) for a QuickJS GC sweep.
    if (b->size & BLK_FREE) {
        static bool dbl_once = false;
        if (!dbl_once) {
            dbl_once = true;
            klogf("KFREE DOUBLE p=0x%x size=%u (ignored)\n",
                  (unsigned)(uintptr_t)p, (unsigned)blk_size(b));
        }
        return;
    }
    b->size |= BLK_FREE;
    b->next = s_free;
    s_free = b;
}

void* balloc(size_t sz) {
    if (sz == 0) sz = 8;
    uint32_t need = ((uint32_t)sz + 15) & ~15u;
    Blk** pp = &s_bfree;
    while (*pp) {
        if (blk_size(*pp) >= need) {
            Blk* b = *pp;
            *pp = b->next;
            b->size = blk_size(b);
            browser_mem_used_bytes += b->size;
            return (void*)(b + 1);
        }
        pp = &(*pp)->next;
    }
    void* r = b_alloc_from_bump(need);
    if (r) browser_mem_used_bytes += need;
    return r;
}

void bfree(void* p) {
    if (!p) return;
    Blk* b = (Blk*)p - 1;
    uint32_t sz = blk_size(b);
    if (browser_mem_used_bytes >= sz) browser_mem_used_bytes -= sz;
    b->next = s_bfree;
    s_bfree = b;
}

void* brealloc(void* p, size_t sz) {
    if (!p) return balloc(sz);
    Blk* b = (Blk*)p - 1;
    uint32_t old = blk_size(b);
    if ((uint32_t)sz <= old) return p;
    void* np = balloc(sz);
    if (!np) return 0;
    memcpy(np, p, old);
    bfree(p);
    return np;
}

void* krealloc(void* p, size_t sz) {
    if (!p) return kalloc(sz);
    Blk* b = (Blk*)p - 1;
    uint32_t old = blk_size(b);
    if ((uint32_t)sz <= old) return p;
    void* np = kalloc(sz);
    if (!np) return 0;
    memcpy(np, p, old);
    kfree(p);
    return np;
}

// ===================== screen =====================
static Screen s_screen;
static uint32_t s_lfb = 0;

Screen* platform_screen() { return &s_screen; }
void platform_present() {
    // Dedicated GPU backends (virtio-gpu) need an explicit upload+flush so
    // the emulated display shows the new pixels. Real hardware and the
    // bochs-display BAR framebuffer are directly visible (no-op).
    nefu::drivers::gpu_present();
}

// ===================== hour =====================
static volatile uint32_t s_ticks = 0;   // 100Hz
static volatile uint32_t s_tick_ms = 0;

uint32_t platform_tick_ms() { return s_tick_ms; }

// bare kernel has no OS image decoder; caller falls back to built-in bmp/ppm
bool platform_decode_image(const uint8_t* data, uint32_t size, Surface& out) {
    (void)data; (void)size; out.addr = 0; return false;
}

// bare kernel has no TrueType font data; caller falls back to bitmap font
bool platform_ttf_text(const char* utf8, int px, int& out_w, int& out_h, uint8_t*& out_rgba) {
    (void)utf8; (void)px; out_w = 0; out_h = 0; out_rgba = 0; return false;
}

void platform_ttf_free(uint8_t* p) { (void)p; }

// ===================== IDT / PIC / PIT =====================
struct IDTEntry {
    uint16_t off_low;
    uint16_t sel;
    uint8_t zero;
    uint8_t attr;
    uint16_t off_mid;
    uint32_t off_high;
    uint32_t zero2;
} __attribute__((packed));

static IDTEntry* s_idt = (IDTEntry*)0x5000;

static void idt_set(int i, void* handler) {
    uint64_t off = (uint64_t)handler;
    s_idt[i].off_low = (uint16_t)(off & 0xFFFF);
    s_idt[i].sel = 0x08;
    s_idt[i].zero = 0;
    s_idt[i].attr = 0x8E;
    s_idt[i].off_mid = (uint16_t)((off >> 16) & 0xFFFF);
    s_idt[i].off_high = (uint32_t)(off >> 32);
    s_idt[i].zero2 = 0;
}

static void pic_eoi(int irq) {
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

// /
// Keyboard event queue. Every keystroke produces two events (press and
// release), and the queue is filled from the keyboard IRQ while the main
// loop drains it once per frame. A 64 entry queue therefore overflowed after
// roughly 30 fast characters and silently dropped the rest - typing a URL
// into the shell lost everything past the first few letters. 512 entries
// cover a fast typist with room to spare and cost only a few KB of .bss.
#define KQ_MAX 512
struct KEvent { int kc; char ascii; bool down; };
static volatile int s_kq_head = 0, s_kq_tail = 0;
static KEvent s_kq[KQ_MAX];
static void kq_push(int kc, char ac, bool down) {
    int n = (s_kq_tail + 1) % KQ_MAX;
    if (n == s_kq_head) {
        // Queue overflow: report it once. Dropping the event silently makes
        // fast typing lose characters, which is very hard to diagnose from
        // the outside.
        static int dropped = 0;
        if (dropped == 0) outb(0xE9, '!');
        dropped++;
        return;
    }
    s_kq[s_kq_tail].kc = kc;
    s_kq[s_kq_tail].ascii = ac;
    s_kq[s_kq_tail].down = down;
    s_kq_tail = n;
}


static int s_mx = 400, s_my = 300;
static uint8_t s_mb = 0;
static volatile bool s_mouse_dirty = false;
static int s_mouse_pkt = 0;
static uint8_t s_mouse_buf[3];
static volatile int s_mouse_dx = 0, s_mouse_dy = 0;


static bool s_shift = false, s_caps = false;

// (set 1): kc!=0,
struct KeyRow { uint8_t sc; int kc; char unshifted; char shifted; };
static const KeyRow KEYMAP[] = {
    { 0x01, KEY_ESC,     0, 0 },
    { 0x0E, KEY_BACKSPACE, 0, 0 },
    { 0x0F, KEY_TAB,     0, 0 },
    { 0x1C, KEY_ENTER,   0, 0 },
    { 0x39, KEY_SPACE,   0, 0 },
    { 0x47, KEY_HOME,    0, 0 },
    { 0x48, KEY_UP,      0, 0 },
    { 0x49, KEY_PGUP,    0, 0 },
    { 0x4B, KEY_LEFT,    0, 0 },
    { 0x4D, KEY_RIGHT,   0, 0 },
    { 0x4F, KEY_END,     0, 0 },
    { 0x50, KEY_DOWN,    0, 0 },
    { 0x51, KEY_PGDN,    0, 0 },
    { 0x52, 0, 0, 0 },               // Ins (ignore)
    { 0x53, KEY_DEL,     0, 0 },
    { 0x3B, KEY_F1, 0, 0 }, { 0x3C, KEY_F2, 0, 0 }, { 0x3D, KEY_F3, 0, 0 },
    { 0x3E, KEY_F4, 0, 0 }, { 0x3F, KEY_F5, 0, 0 }, { 0x40, KEY_F6, 0, 0 },
    { 0x41, KEY_F7, 0, 0 }, { 0x42, KEY_F8, 0, 0 }, { 0x43, KEY_F9, 0, 0 },
    { 0x44, KEY_F10, 0, 0 }, { 0x57, KEY_F11, 0, 0 }, { 0x58, KEY_F12, 0, 0 },
    // Windows / Super keys. Without them the whole "Win + key" shortcut
    // family is dead on the bare kernel: the desktop reads KEY_FAST for the
    // modifier flag, but the key table had no entry that produced it, so
    // Win+B (browser), Win+R (terminal) and friends never fired.
    { 0x5B, KEY_FAST, 0, 0 }, { 0x5C, KEY_FAST, 0, 0 },

    { 0x02, 0, '1', '!' }, { 0x03, 0, '2', '@' }, { 0x04, 0, '3', '#' },
    { 0x05, 0, '4', '$' }, { 0x06, 0, '5', '%' }, { 0x07, 0, '6', '^' },
    { 0x08, 0, '7', '&' }, { 0x09, 0, '8', '*' }, { 0x0A, 0, '9', '(' },
    { 0x0B, 0, '0', ')' },

    { 0x0C, 0, '-', '_' }, { 0x0D, 0, '=', '+' },
    { 0x1A, 0, '[', '{' }, { 0x1B, 0, ']', '}' },
    { 0x27, 0, ';', ':' }, { 0x28, 0, '\'', '"' }, { 0x29, 0, '`', '~' },
    { 0x2B, 0, '\\', '|' }, { 0x33, 0, ',', '<' }, { 0x34, 0, '.', '>' },
    { 0x35, 0, '/', '?' },
    // letter
    { 0x1E, 0, 'a', 'A' }, { 0x30, 0, 'b', 'B' }, { 0x2E, 0, 'c', 'C' },
    { 0x20, 0, 'd', 'D' }, { 0x12, 0, 'e', 'E' }, { 0x21, 0, 'f', 'F' },
    { 0x22, 0, 'g', 'G' }, { 0x23, 0, 'h', 'H' }, { 0x17, 0, 'i', 'I' },
    { 0x24, 0, 'j', 'J' }, { 0x25, 0, 'k', 'K' }, { 0x26, 0, 'l', 'L' },
    { 0x32, 0, 'm', 'M' }, { 0x31, 0, 'n', 'N' }, { 0x18, 0, 'o', 'O' },
    { 0x19, 0, 'p', 'P' }, { 0x10, 0, 'q', 'Q' }, { 0x13, 0, 'r', 'R' },
    { 0x1F, 0, 's', 'S' }, { 0x14, 0, 't', 'T' }, { 0x16, 0, 'u', 'U' },
    { 0x2F, 0, 'v', 'V' }, { 0x11, 0, 'w', 'W' }, { 0x2D, 0, 'x', 'X' },
    { 0x15, 0, 'y', 'Y' }, { 0x2C, 0, 'z', 'Z' },
};

static void kbd_scancode(uint8_t sc) {
    // debug: emit the raw scancode on the debugcon port
    outb(0xE9, 'S');
    static const char hexd[] = "0123456789ABCDEF";
    outb(0xE9, hexd[(sc >> 4) & 0xF]);
    outb(0xE9, hexd[sc & 0xF]);
    bool down = !(sc & 0x80);
    uint8_t code = sc & 0x7F;
    if (code == 0x2A || code == 0x36) { s_shift = down; kq_push(KEY_SHIFT, 0, down); return; }
    if (code == 0x3A) {
        if (down) { s_caps = !s_caps; kq_push(KEY_CAPS, 0, true); }
        return;
    }
    if (code == 0x1D) { kq_push(KEY_CTRL, 0, down); return; }
    if (code == 0x38) { kq_push(KEY_ALT, 0, down); return; }
    if (code == 0xE0 || code == 0xE1) return;
    for (size_t i = 0; i < sizeof(KEYMAP) / sizeof(KEYMAP[0]); i++) {
        if (KEYMAP[i].sc == code) {
            const KeyRow& r = KEYMAP[i];
            if (r.kc != 0) {
                // The space bar is a text key as well as an action key: give
                // it an ascii character so that text fields (the shell, the
                // browser address bar) accept it, while the keycode stays
                // KEY_SPACE for the apps that use it as "activate".
                kq_push(r.kc, r.kc == KEY_SPACE ? ' ' : 0, down);
                return;
            }
            if (down) {
                char c;
                bool upper = s_shift != s_caps;
                if (r.unshifted >= 'a' && r.unshifted <= 'z') {
                    c = upper ? r.shifted : r.unshifted;
                } else {
                    c = s_shift ? r.shifted : r.unshifted;
                }
                kq_push(0, c, true);
            }
            return;
        }
    }
}

static void mouse_byte(uint8_t b) {
    if (b == 0xFA) return;            // PS/2 ACK - never part of a data packet
    if (s_mouse_pkt == 0) {
        if (!(b & 0x08)) return;      // byte0
        s_mouse_buf[0] = b;
        s_mouse_pkt = 1;
        return;
    }
    s_mouse_buf[s_mouse_pkt] = b;
    s_mouse_pkt++;
    if (s_mouse_pkt < 3) return;
    s_mouse_pkt = 0;
    uint8_t b0 = s_mouse_buf[0];
    int dx = (int)(int8_t)s_mouse_buf[1];
    int dy = (int)(int8_t)s_mouse_buf[2];
    if (b0 & 0x40) dx += (s_mouse_buf[1] < 128) ? 256 : -256;
    if (b0 & 0x80) dy += (s_mouse_buf[2] < 128) ? 256 : -256;
    s_mouse_dx += dx;
    s_mouse_dy += dy;
    s_mb = (uint8_t)(b0 & 0x07);   // PS/2 byte0: bit0=left, bit1=right, bit2=middle (1=pressed)
    s_mouse_dirty = true;
}

// IRQ handlers must stay integer-only: GCC's interrupt attribute rejects SSE
// instructions inside an ISR ("SSE instructions aren't allowed in an interrupt
// service routine"), so the handlers below cannot touch the FPU state of the
// interrupted context, and everything they call (scancode/mouse decode, PIC
// EOI) is pure integer/port-IO code.  The main loop's XMM registers are thus
// preserved across interrupts by construction.
__attribute__((interrupt)) static void irq0_handler(void* frame) {
    (void)frame;
    s_ticks++;
    s_tick_ms = s_ticks * 10;
    pic_eoi(0);
}
__attribute__((interrupt)) static void irq1_handler(void* frame) {
    (void)frame;
    __asm__ volatile("movw $0xE9, %%dx; movb $'K', %%al; outb %%al, %%dx" ::: "dx", "ax");
    uint8_t sc = inb(0x60);
    kbd_scancode(sc);
    pic_eoi(1);
}
__attribute__((interrupt)) static void irq12_handler(void* frame) {
    (void)frame;
    __asm__ volatile("movw $0xE9, %%dx; movb $'M', %%al; outb %%al, %%dx" ::: "dx", "ax");
    mouse_byte(inb(0x60));
    pic_eoi(12);
}
__attribute__((interrupt)) static void spurious_handler(void* frame) { (void)frame; }
extern "C" void nefu_ud_syscall_stub(void);   // elfrun.cpp: guest syscall emulation (#UD -> Linux syscall)
static void idt_init() {
    // spurious, IRQ
    for (int i = 0; i < 256; i++) idt_set(i, (void*)spurious_handler);
    idt_set(6, (void*)nefu_ud_syscall_stub);  // #UD: 0f 05 syscall -> elf syscall emulator
    idt_set(32, (void*)irq0_handler);
    idt_set(33, (void*)irq1_handler);
    idt_set(44, (void*)irq12_handler);
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) idtr;
    idtr.limit = 256 * 16 - 1;
    idtr.base = 0x5000;
    __asm__ volatile("lidt %0" : : "m"(idtr));
}

static void pic_init() {
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait();
    outb(0xA1, 0x28); io_wait();
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();
    outb(0x21, 0xF8);   // allow IRQ0/1/2
    outb(0xA1, 0xEF);   // allow IRQ12
}

static void pit_init() {
    uint16_t div = 11931;   // 100Hz
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(div & 0xFF));
    outb(0x40, (uint8_t)(div >> 8));
}

static void ps2_init() {

    outb(0x64, 0xA8); io_wait();
    // , IRQ12
    outb(0x64, 0x20); io_wait();
    uint8_t cb = inb(0x60);
    cb |= 0x02;        // enable aux IRQ
    cb &= ~0x20;       // enable aux clock
    cb &= ~0x10;       // enable keyboard IRQ
    outb(0x64, 0x60); io_wait();
    outb(0x60, cb); io_wait();

    outb(0x64, 0xD4); io_wait();
    outb(0x60, 0xF4); io_wait();
    // Wait for the 0xFA ACK of the enable command (with timeout), so it never
    // leaks into the motion stream.
    for (int i = 0; i < 10000; i++) {
        if (inb(0x64) & 1) { if (inb(0x60) == 0xFA) break; }
        io_wait();
    }
}

// ===================== RTC =====================
// CMOS port access stays here because the memory-size probe below also needs it.
// The date/time decode does not: that single implementation lives in
// bare_libc.cpp (nefu_rtc_read_civil).
static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}
// The CMOS decode itself lives in bare_libc.cpp (nefu_rtc_read_civil) so the
// kernel UI and the C library can never disagree about what the chip said.
// This file used to decode its own copy, one register off: 0x06 (weekday) as
// the day, 0x07 (day) as the month, 0x08 (month) as the year. The month range
// check then rejected everything past the 12th, so `date` reported "RTC
// unavailable" for two thirds of every month.
extern "C" {
int  nefu_rtc_read_civil(int*, int*, int*, int*, int*, int*);
long long nefu_now_utc(void);
int  nefu_tz_offset_minutes(void);
void nefu_set_tz_offset_minutes(int);
int  nefu_rtc_is_utc(void);
void nefu_set_rtc_is_utc(int);
}

uint32_t platform_seconds_of_day() {
    // Local wall clock = true UTC + the zone offset, folded into one day. The
    // taskbar, the clock app and the lock screen all draw this.
    long long local = nefu_now_utc() + (long long)nefu_tz_offset_minutes() * 60;
    long long sod = local % 86400;
    if (sod < 0) sod += 86400;
    return (uint32_t)sod;
}

int  platform_tz_offset_minutes() { return nefu_tz_offset_minutes(); }
void platform_set_tz_offset_minutes(int minutes) { nefu_set_tz_offset_minutes(minutes); }
bool platform_rtc_is_utc() { return nefu_rtc_is_utc() != 0; }
void platform_set_rtc_is_utc(bool on) { nefu_set_rtc_is_utc(on ? 1 : 0); }

bool platform_rtc_date(DateInfo* out) {
    if (!out) return false;
    int y, mo, d, h, mi, s;
    if (!nefu_rtc_read_civil(&y, &mo, &d, &h, &mi, &s)) return false;
    out->year = y;
    out->month = mo;
    out->day = d;
    out->hour = h;
    out->min = mi;
    out->sec = s;
    // weekday straight from the civil date (Sakamoto), 0 = Sunday; the chip's
    // own weekday register is redundant and has been seen to disagree with the
    // date after a firmware clock reset.
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int yy = y - (mo < 3 ? 1 : 0);
    out->dow = (yy + yy / 4 - yy / 100 + yy / 400 + t[mo - 1] + d) % 7;
    return true;
}

// ===================== debug / power off =====================
void platform_dbg(const char* s) {
    while (*s) uart_putc(*s++);
}

void platform_poweroff() {
    // QEMU ACPI power off
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);   // Bochs
    for (;;) { __asm__ volatile("cli; hlt"); }
}

void platform_reboot() {
    // Reset via the 8042 keyboard controller (standard x86 reboot method)
    uint8_t good = 0x02;
    while (good & 0x02) good = inb(0x64);
    outb(0x64, 0xFE);  // Pulse the CPU reset line
    for(;;) { __asm__ volatile("hlt"); }
}

void platform_suspend() {
    // Bare-metal standby: blank the screen, wait for a key press to wake
    Screen* s = platform_screen();
    // Clear the screen to black
    for (int y=0; y<s->height; y++) {
        uint32_t* row = (uint32_t*)(s->addr + y*s->pitch);
        for (int x=0; x<s->width; x++) row[x] = 0;
    }
    // Wait for keyboard input to wake
    for(;;) {
        __asm__ volatile("hlt");
        // A key event wakes back into the lock-screen flow
        if (s_kq_head != s_kq_tail) break;
    }
}

// Read CPU info via the CPUID instruction
static void cpuid(uint32_t leaf, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
    __asm__ volatile("cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(leaf));
}

bool platform_hw_info(HwInfo* out) {
    memset(out, 0, sizeof(*out));
    // Read the CPU model
    uint32_t a,b,c,d;
    char model[49];
    memset(model, 0, sizeof(model));
    cpuid(0x80000002, &a,&b,&c,&d);
    memcpy(model+0, &a, 4); memcpy(model+4, &b, 4); memcpy(model+8, &c, 4); memcpy(model+12, &d, 4);
    cpuid(0x80000003, &a,&b,&c,&d);
    memcpy(model+16, &a, 4); memcpy(model+20, &b, 4); memcpy(model+24, &c, 4); memcpy(model+28, &d, 4);
    cpuid(0x80000004, &a,&b,&c,&d);
    memcpy(model+32, &a, 4); memcpy(model+36, &b, 4); memcpy(model+40, &c, 4); memcpy(model+44, &d, 4);
    strncpy(out->cpu_model, model, 63);
    // Read the CPU clock from CPUID leaf 0x16
    cpuid(0x16, &a,&b,&c,&d);
    out->cpu_mhz = a;
    out->cpu_cores = 1;
    // Read memory size from CMOS (extended memory above the low 16 MB)
    uint16_t mem_kb = cmos_read(0x17) | (cmos_read(0x18) << 8);
    (void)mem_kb;
    // Logical system cap: 4 GB (kernel 512MB + browser 2GB + reserve 1.5GB)
    out->mem_total_mb = (uint64_t)SYS_MEM_TOTAL_MB;
    strncpy(out->bios_vendor, "SeaBIOS/QEMU", 31);
    strncpy(out->bios_version, "1.0.0-nefuOS", 31);
    return true;
}

// UEFI config lives in CMOS / CMOS battery-backed RAM at offsets 0x10-0x30
bool platform_uefi_load(UefiConfig* out) {
    memset(out, 0, sizeof(*out));
    // Default values
    strncpy(out->username, "user", 31);
    strncpy(out->password_hash, "", 63);
    out->boot_timeout = 3;
    out->boot_splash = true;
    strncpy(out->wallpaper_boot, "blue", 31);
    strncpy(out->wallpaper_lock, "dark", 31);
    return true;
}

bool platform_uefi_save(const UefiConfig* cfg) {
    (void)cfg;
    // Bare-metal CMOS writes can be extended; just return success here
    return true;
}

void platform_mem_stats(uint32_t* used, uint32_t* total) {
    *used = s_arena_next - ARENA_BASE;
    *total = ARENA_SIZE;
}

const char* platform_name() { return "bare (x86_64)"; }

// NVFS image persistence on the first non-removable ATA drive (QEMU -hda).
// Implemented with the ATA PIO driver below. Disk layout: LBA 0 reserved,
// LBA 1 = uint32 LE image size, LBA 2.. = image data.
bool platform_fs_load(uint8_t** out, uint32_t* out_size);
void platform_fs_save(const uint8_t* data, uint32_t size);

// ===================== =====================
static bool sb_audio_init(void);   // defined below (SB16 driver)

extern "C" void nefuos_kernel_main(void* info) {
    __asm__ volatile("movw $0xE9, %%dx; movb $'1', %%al; outb %%al, %%dx" ::: "dx", "ax");
    uint32_t* bi = (uint32_t*)info;
    s_lfb = bi[0];
    s_screen.addr = (uint8_t*)s_lfb;
    s_screen.width = (int)bi[1];
    s_screen.height = (int)bi[2];
    s_screen.pitch = (int)bi[3];
    s_screen.bpp = (int)bi[4];
    if (s_screen.bpp != 24) s_screen.bpp = 32;
    __asm__ volatile("movw $0xE9, %%dx; movb $'2', %%al; outb %%al, %%dx" ::: "dx", "ax");

    // GPU driver: probe the PCI display adapter and pick the best framebuffer
    // backend. QEMU virtio-gpu gets a full scanout resource; bochs-display
    // maps BAR0; real Intel/AMD/NVIDIA hardware keeps the boot GOP/VBE LFB.
    nefu::drivers::gpu_log_adapters();
    {
        uint8_t* fb = s_screen.addr;
        int w = s_screen.width, h = s_screen.height, pitch = s_screen.pitch, bpp = s_screen.bpp;
        nefu::drivers::gpu_setup_framebuffer(&fb, &w, &h, &pitch, &bpp);
        // pitch is part of the comparison: an iGPU takeover can keep the very
        // same buffer and size while correcting a wrong boot pitch, and dropping
        // that update would leave every drawing pass striding into the old one.
        if (fb != s_screen.addr || w != s_screen.width || h != s_screen.height ||
            pitch != s_screen.pitch || bpp != s_screen.bpp) {
            s_lfb = (uint32_t)(uintptr_t)fb;
            s_screen.addr = fb;
            s_screen.width = w;
            s_screen.height = h;
            s_screen.pitch = pitch;
            s_screen.bpp = bpp;
        }
    }

    uart_init();
    platform_dbg("\nnefuOS bare kernel: LFB=0x");
    {
        char tmp[64];
        ksprintf(tmp, sizeof(tmp), " arena0=0x%x\n", (unsigned)s_arena_next);
        platform_dbg(tmp);
    }
    for (int i = 28; i >= 0; i -= 4) {
        int d = (int)((s_lfb >> i) & 0xF);
        uart_putc((char)(d < 10 ? '0' + d : 'A' + d - 10));
    }
    // diag removed: solid-fill test was for LFB debugging only
    __asm__ volatile("movw $0xE9, %%dx; movb $'X', %%al; outb %%al, %%dx" ::: "dx", "ax");
    {
        char tmp[48];
        ksprintf(tmp, sizeof(tmp), " screen=%dx%dx%d pitch=%d\n", s_screen.width, s_screen.height, (int)bi[4] ? (int)bi[4] : 32, s_screen.pitch);
        platform_dbg(tmp);
    }
    __asm__ volatile("movw $0xE9, %%dx; movb $'Y', %%al; outb %%al, %%dx" ::: "dx", "ax");
    __asm__ volatile("movw $0xE9, %%dx; movb $'3', %%al; outb %%al, %%dx" ::: "dx", "ax");

    idt_init();
    __asm__ volatile("movw $0xE9, %%dx; movb $'A', %%al; outb %%al, %%dx" ::: "dx", "ax");
    pic_init();
    pit_init();
    ps2_init();
    __asm__ volatile("movw $0xE9, %%dx; movb $'B', %%al; outb %%al, %%dx" ::: "dx", "ax");
    __asm__ volatile("movw $0xE9, %%dx; movb $'Z', %%al; outb %%al, %%dx" ::: "dx", "ax");

    nefuos_init();

    // audio: the integer engine probes real PCI sound cards (AC'97 / HDA)
    // first, then the ISA SB16 fallback (headless serial runs can verify)
    if (nefu::audio::engine_init())
        platform_dbg("audio: engine ready on PCI sound card\n");
    else if (sb_audio_init())
        platform_dbg("audio: sb16 detected at 0x220 (DSP 0xAA)\n");
    else
        platform_dbg("audio: no sound card (run QEMU with -device AC97 or -device intel-hda)\n");

    __asm__ volatile("movw $0xE9, %%dx; movb $'C', %%al; outb %%al, %%dx" ::: "dx", "ax");

    __asm__ volatile("movw $0xE9, %%dx; movb $'D', %%al; outb %%al, %%dx" ::: "dx", "ax");
    nefuos_frame();   // manual first frame before interrupts
    __asm__ volatile("movw $0xE9, %%dx; movb $'E', %%al; outb %%al, %%dx" ::: "dx", "ax");
    // diag removed: solid-fill freeze was for LFB debugging only

    __asm__ volatile("sti");

    uint32_t last = 0;
    int frames = 0;
    for (;;) {

        while (s_kq_head != s_kq_tail) {
            KEvent ev = s_kq[s_kq_head];
            s_kq_head = (s_kq_head + 1) % KQ_MAX;
            nefuos_handle_key(ev.kc, ev.ascii, ev.down);
        }

        if (s_mouse_dirty) {
            s_mouse_dirty = false;
            s_mx += s_mouse_dx;
            s_my -= s_mouse_dy;          // PS/2 dy>0 = up; screen y grows downward
            s_mouse_dx = 0; s_mouse_dy = 0;
            if (s_mx < 0) s_mx = 0;
            if (s_mx >= s_screen.width) s_mx = s_screen.width - 1;
            if (s_my < 0) s_my = 0;
            if (s_my >= s_screen.height) s_my = s_screen.height - 1;
            static int s_mouse_log = 0;
            if (s_mouse_log < 6) { s_mouse_log++; klogf("mouse pos=%d,%d b=%d\n", s_mx, s_my, s_mb); }
            nefuos_handle_mouse(s_mx, s_my, s_mb);
        }
        uint32_t now = s_tick_ms;
        if (now != last) {
            last = now;
            nefuos_tick();
            nefuos_frame();
            if (frames < 3) {
                frames++;
                __asm__ volatile("movw $0xE9, %%dx; movb $'F', %%al; outb %%al, %%dx" ::: "dx", "ax");
            }
        }
        __asm__ volatile("hlt");
    }
}

} // namespace nefu

// ===================== net (bare) =====================
#include "../../core/net/net.h"
namespace nefu {

bool platform_net_get(NetAdapterInfo* out) {
    if (!out) return false;
    out->up = g_net.up;
    for (int i = 0; i < 6; i++) out->mac[i] = g_net.mac[i];
    out->ip = g_net.ip;
    out->gw = g_net.gw;
    out->mask = g_net.netmask;
    strncpy(out->name, "NIC", 48);
    out->name[47] = 0;
    return true;
}

int platform_wifi_scan(WifiNetInfo* list, int max) {
    (void)list; (void)max;
    return 0;   // e1000 is wired; no Wi-Fi on bare metal
}

bool platform_ping(uint32_t ip, int timeout_ms) {
    return net_ping(ip, timeout_ms);
}

bool platform_http_get(const char* url, uint8_t** out, uint32_t* out_size) {
    // bare: in-house TCP stack + native TLS 1.3 via core/net/httpbare
    return nefu::net::httpbare_request("GET", url, 0, 0, 0, 0, out, out_size);
}

// Generic HTTP request (bare): in-house TCP stack + native TLS 1.3. Supports
// the extra headers the npkg slowAES challenge needs (Cookie: __test=...).
bool platform_http_request(const char* method, const char* url,
                           const char* headers,
                           const uint8_t* body, uint32_t body_len,
                           int* out_status, uint8_t** out_body, uint32_t* out_body_len) {
    return nefu::net::httpbare_request(method, url, headers, body, body_len,
                                       out_status, out_body, out_body_len);
}

// Cookies set by the most recent request. The bare stack has no shared cookie
// store, so httpbare keeps the flattened Set-Cookie pairs of the last exchange
// and the JS document jar adopts them right after the call returns.
const char* platform_http_last_set_cookie() {
    return nefu::net::httpbare_last_set_cookie();
}

// Response headers of the last exchange, flattened to "Name: value" lines.
// The bare stack parses the header block itself, so it can report exactly what
// the server sent - the JS Headers object and XHR getAllResponseHeaders() read
// this instead of the placeholder they used to synthesise.
const char* platform_http_last_headers() {
    return nefu::net::httpbare_last_headers();
}

// Streaming HTTP request (bare).
//
// The bare stack has no background reader thread, so the response is fetched
// with the regular client (which already handles chunked transfer, gzip
// decompression and redirects) and then handed to the callback in one piece.
// The callback contract is satisfied either way - the parser on the other
// side is incremental - and this keeps a single implementation of the
// wire format.
bool platform_http_stream_send(const char* method, const char* url,
                               const char* headers,
                               const uint8_t* body, uint32_t body_len,
                               void* ctx,
                               bool (*cb)(void* ctx, const uint8_t* data, uint32_t len),
                               int* out_status) {
    if (!cb) return false;
    uint8_t* rbody = 0;
    uint32_t len = 0;
    int status = 0;
    if (!nefu::net::httpbare_fetch(method ? method : "GET", url, headers,
                                   body, body_len, &status, &rbody, &len)) {
        if (out_status) *out_status = status;
        return false;
    }
    if (out_status) *out_status = status;
    bool ok = true;
    if (rbody && len > 0) ok = cb(ctx, rbody, len);
    if (rbody) kfree(rbody);
    return ok;
}

bool platform_http_stream_request(const char* method, const char* url,
                                  const char* headers, void* ctx,
                                  bool (*cb)(void* ctx, const uint8_t* data, uint32_t len),
                                  int* out_status) {
    return platform_http_stream_send(method, url, headers, 0, 0, ctx, cb, out_status);
}

// Chunked GET (bare): used by the browser to stream a page into the parser.
bool platform_http_stream(const char* url, void* ctx,
                          bool (*cb)(void* ctx, const uint8_t* data, uint32_t len)) {
    return platform_http_stream_request("GET", url, 0, ctx, cb, 0);
}

// ===================== WebSocket client (bare) =====================
// Reuses the in-house TCP stack + native TLS 1.3 client + nefu::netproto WS
// framing. There is no background thread on bare metal, so the JS pump loop's
// platform_ws_peek synchronously drives recv + frame reassembly. Messages are
// queued so bursts arriving in one recv are never lost.
namespace {
struct WsBare {
    bool     used;
    int      state;          // 0 connecting, 1 open, 2 closed
    bool     secure;
    int      fd;             // tcp fd (-1 if none)
    nefu::tls::Tls13Conn* tls;
    uint32_t seed;           // masking LCG seed
    uint8_t* rx;             // decrypted accumulator
    uint32_t rx_len, rx_cap;
    uint8_t* frag;           // current (fragmented) message accumulator
    uint32_t frag_len, frag_cap;
    bool     frag_bin;
    static const int QN = 16;
    uint8_t* qbuf[QN]; int qlen[QN]; bool qbin[QN];
    int qn, qhead;           // queued message count + pop index
    uint8_t* cur; int cur_len, cur_off; bool cur_valid; bool cur_bin;
};
static WsBare s_ws[8];
static const int WS_MAX = (int)(sizeof(s_ws) / sizeof(s_ws[0]));

static int ws_tls_recv(void* ctx, uint8_t* buf, int maxlen, int timeout_ms) {
    WsBare* c = (WsBare*)ctx;
    return tcp_recv(c->fd, buf, maxlen, timeout_ms);
}
static int ws_tls_send(void* ctx, const uint8_t* buf, int len) {
    WsBare* c = (WsBare*)ctx;
    return tcp_send(c->fd, buf, len);
}

// 16 random bytes -> 24-char base64 client key (RFC 6455).
static void ws_make_key(char* out) {
    static const char* a =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint8_t r[16];
    uint32_t s = (uint32_t)platform_tick_ms() ^ 0x9E3779B9u;
    for (int i = 0; i < 16; i++) {
        s = s * 2654435761u + 40503u + (uint32_t)i;
        r[i] = (uint8_t)(s >> 13);
    }
    int o = 0;
    for (int i = 0; i < 16; i += 3) {
        uint8_t b0 = r[i];
        uint8_t b1 = (i + 1 < 16) ? r[i + 1] : 0;
        uint8_t b2 = (i + 2 < 16) ? r[i + 2] : 0;
        uint32_t n = ((uint32_t)b0 << 16) | ((uint32_t)b1 << 8) | b2;
        out[o++] = a[(n >> 18) & 63]; out[o++] = a[(n >> 12) & 63];
        out[o++] = (i + 1 < 16) ? a[(n >> 6) & 63] : '=';
        out[o++] = (i + 2 < 16) ? a[n & 63] : '=';
    }
}

static void ws_rx_append(WsBare* c, const uint8_t* d, uint32_t n) {
    if (c->rx_len + n > c->rx_cap) {
        uint32_t nc = c->rx_cap ? c->rx_cap : 4096;
        while (nc < c->rx_len + n) nc *= 2;
        uint8_t* np = (uint8_t*)kalloc(nc);
        if (!np) return;
        if (c->rx) { memcpy(np, c->rx, c->rx_len); kfree(c->rx); }
        c->rx = np; c->rx_cap = nc;
    }
    memcpy(c->rx + c->rx_len, d, n);
    c->rx_len += n;
}

static void ws_queue(WsBare* c, bool bin) {
    if (c->qn >= WsBare::QN) return;             // drop on overflow
    int idx = (c->qhead + c->qn) % WsBare::QN;
    uint8_t* m = c->frag_len ? (uint8_t*)kalloc(c->frag_len) : 0;
    if (c->frag_len) memcpy(m, c->frag, c->frag_len);
    c->qbuf[idx] = m; c->qlen[idx] = (int)c->frag_len; c->qbin[idx] = bin;
    c->qn++;
    c->frag_len = 0;
}

// Drive recv + reassemble complete messages into the queue.
static void ws_pump(WsBare* c) {
    if (!c || c->state != 1) return;
    uint8_t tmp[4096];
    int r = c->tls ? nefu::tls::tls13_recv(c->tls, tmp, (int)sizeof(tmp), 150)
                   : tcp_recv(c->fd, tmp, (int)sizeof(tmp), 150);
    // Distinguish "nothing available yet" from "the peer closed the
    // connection": a closed socket must retire the session instead of being
    // polled forever. TLS reports EOF as 0 and a timeout as -1; the bare TCP
    // layer uses the same convention.
    if (r == 0) {
        c->state = 2;            // closed by peer
        c->used = false;
        return;
    }
    if (r < 0) return;           // timeout (or error): retry on the next pump
    ws_rx_append(c, tmp, (uint32_t)r);
    for (;;) {
        if (c->rx_len < 2) break;
        uint8_t b1 = c->rx[1];
        uint32_t pl = b1 & 0x7Fu;
        int ho = 2;
        if (pl == 126) {
            if (c->rx_len < 4) break;
            pl = ((uint32_t)c->rx[2] << 8) | c->rx[3]; ho = 4;
        } else if (pl == 127) {
            if (c->rx_len < 10) break;
            pl = ((uint32_t)c->rx[6] << 24) | ((uint32_t)c->rx[7] << 16) |
                 ((uint32_t)c->rx[8] << 8) | c->rx[9]; ho = 10;
        }
        if (b1 & 0x80) ho += 4;                   // mask
        if (c->rx_len < (uint32_t)ho + pl) break; // incomplete frame
        nefu::netproto::WsFrame f;
        int hlen = nefu::netproto::ws_decode_frame(c->rx, (int)c->rx_len, f);
        if (hlen < 0) { c->rx_len = 0; break; }   // malformed: drop stream
        uint8_t op = f.opcode;
        if (op == nefu::netproto::WS_OP_CLOSE) {
            uint8_t cb[128];
            int cn = nefu::netproto::ws_build_close(cb, 1000, "bye", true, &c->seed);
            if (c->tls) nefu::tls::tls13_send(c->tls, cb, cn);
            else        tcp_send(c->fd, cb, cn);
            c->state = 2;
            break;
        } else if (op == nefu::netproto::WS_OP_PING) {
            uint8_t pb[256];
            int pn = nefu::netproto::ws_build_pong(pb, f.payload, (int)f.payload_len, true, &c->seed);
            if (c->tls) nefu::tls::tls13_send(c->tls, pb, pn);
            else        tcp_send(c->fd, pb, pn);
        } else if (op == nefu::netproto::WS_OP_PONG) {
            // ignore
        } else {  // data: text(1) / binary(2) / continuation(0)
            bool start = (op == nefu::netproto::WS_OP_TEXT ||
                          op == nefu::netproto::WS_OP_BINARY);
            if (start) { c->frag_bin = (op == nefu::netproto::WS_OP_BINARY); c->frag_len = 0; }
            if (f.payload_len > 0) {
                uint32_t need = c->frag_len + f.payload_len;
                if (need > c->frag_cap) {
                    uint32_t nc = c->frag_cap ? c->frag_cap : 4096;
                    while (nc < need) nc *= 2;
                    uint8_t* nf = (uint8_t*)kalloc(nc);
                    if (!nf) { c->frag_len = 0; break; }
                    if (c->frag) { memcpy(nf, c->frag, c->frag_len); kfree(c->frag); }
                    c->frag = nf; c->frag_cap = nc;
                }
                memcpy(c->frag + c->frag_len, f.payload, f.payload_len);
                c->frag_len += f.payload_len;
            }
            if (f.fin) ws_queue(c, c->frag_bin);
        }
        uint32_t total = (uint32_t)hlen + f.payload_len;
        memmove(c->rx, c->rx + total, c->rx_len - total);
        c->rx_len -= total;
    }
}

} // namespace

// The bare-metal TLS client has no header injection hook, so the richer
// handshake falls back to the plain one (host backends supply the real
// implementation).
int platform_ws_open_hdr(const char* url, const char* extra_headers) {
    (void)extra_headers;
    return platform_ws_open(url);
}

int platform_ws_open(const char* url) {
    if (!url || !url[0]) return -1;
    bool secure = true;
    const char* p = strstr(url, "://");
    if (!p) return -1;
    int sl = (int)(p - url);
    if (sl == 2)      { if (strncmp(url, "ws", 2) != 0) return -1;  secure = false; }
    else if (sl == 3) { if (strncmp(url, "wss", 3) != 0) return -1; secure = true; }
    else return -1;
    p += 3;
    const char* slash  = strchr(p, '/');
    const char* qmark  = strchr(p, '?');
    const char* endh   = p + strlen(p);
    if (slash && slash < endh) endh = slash;
    if (qmark && qmark < endh) endh = qmark;
    const char* colon = 0;
    for (const char* q = p; q < endh; q++) if (*q == ':') { colon = q; break; }
    int port = secure ? 443 : 80;
    const char* he = colon ? colon : endh;
    if (colon) {
        int pl = 0; const char* pe = colon + 1;
        while (pe[pl] >= '0' && pe[pl] <= '9') pl++;
        if (pl > 0 && pl <= 5) {
            int v = 0; for (int i = 0; i < pl; i++) v = v * 10 + (pe[i] - '0');
            if (v > 0) port = v;
        }
        he = pe + pl;
    }
    int hl = (int)(he - p);
    if (hl <= 0 || hl >= 128) return -1;
    char host[128]; memcpy(host, p, hl); host[hl] = 0;
    char path[1024];
    if (slash)      { int pl = (int)strlen(slash); if (pl >= (int)sizeof(path)) pl = (int)sizeof(path) - 1; memcpy(path, slash, pl); path[pl] = 0; }
    else if (qmark) { path[0] = '/'; int pl = (int)strlen(qmark); if (pl > (int)sizeof(path) - 2) pl = (int)sizeof(path) - 2; memcpy(path + 1, qmark, pl); path[1 + pl] = 0; }
    else            { path[0] = '/'; path[1] = 0; }

    uint32_t ip = dns_lookup(host);
    if (!ip) { klogf("ws: dns fail %s\n", host); return -1; }

    int slot = -1;
    for (int i = 0; i < WS_MAX; i++) { if (!s_ws[i].used) { slot = i; break; } }
    if (slot < 0) return -1;
    WsBare* c = &s_ws[slot];
    memset(c, 0, sizeof(WsBare));
    c->used = true; c->state = 0; c->secure = secure; c->fd = -1;

    c->fd = tcp_connect(ip, (uint16_t)port, 10000);
    if (c->fd < 0) { c->used = false; return -1; }

    if (secure) {
        char terr[160];
        c->tls = nefu::tls::tls13_connect(ws_tls_recv, ws_tls_send, c, host, terr, sizeof(terr));
        if (!c->tls) {
            klogf("ws: TLS fail %s: %s\n", host, terr);
            tcp_close(c->fd); c->fd = -1; c->used = false; return -1;
        }
    }

    char key[25]; ws_make_key(key); key[24] = 0;
    String hs;
    nefu::netproto::ws_build_client_handshake(host, path, key, hs);
    int hs_len = hs.len();
    int sent = c->tls ? nefu::tls::tls13_send(c->tls, (const uint8_t*)hs.c_str(), hs_len)
                      : tcp_send(c->fd, (const uint8_t*)hs.c_str(), hs_len);
    if (sent != hs_len) {
        klogf("ws: handshake send fail %s (%d/%d)\n", host, sent, hs_len);
        if (c->tls) { nefu::tls::tls13_close(c->tls); c->tls = 0; }
        tcp_close(c->fd); c->fd = -1; c->used = false; return -1;
    }

    // read 101 response
    uint8_t resp[4096]; int resp_len = 0;
    uint8_t tmp[2048];
    uint32_t start = platform_tick_ms();
    bool got = false;
    while (platform_tick_ms() - start < 15000) {
        int r = c->tls ? nefu::tls::tls13_recv(c->tls, tmp, (int)sizeof(tmp), 800)
                       : tcp_recv(c->fd, tmp, (int)sizeof(tmp), 800);
        if (r <= 0) { if (r == 0) break; continue; }
        if ((uint32_t)resp_len + r <= (uint32_t)sizeof(resp)) {
            memcpy(resp + resp_len, tmp, (uint32_t)r); resp_len += r;
        }
        bool end = false;
        for (int i = 0; i + 3 < resp_len; i++)
            if (resp[i]=='\r'&&resp[i+1]=='\n'&&resp[i+2]=='\r'&&resp[i+3]=='\n') { end = true; break; }
        if (!end) for (int i = 0; i + 1 < resp_len; i++)
            if (resp[i]=='\n'&&resp[i+1]=='\n') { end = true; break; }
        if (end) { got = true; break; }
    }
    if (!got) {
        klogf("ws: no 101 from %s\n", host);
        if (c->tls) { nefu::tls::tls13_close(c->tls); c->tls = 0; }
        tcp_close(c->fd); c->fd = -1; c->used = false; return -1;
    }
    char accept[32];
    if (nefu::netproto::ws_parse_server_handshake((const char*)resp, resp_len, accept, sizeof(accept)) != 0) {
        klogf("ws: handshake rejected by %s\n", host);
        if (c->tls) { nefu::tls::tls13_close(c->tls); c->tls = 0; }
        tcp_close(c->fd); c->fd = -1; c->used = false; return -1;
    }
    c->seed = (uint32_t)platform_tick_ms() ^ (uint32_t)port;
    c->state = 1;
    klogf("ws: open %s://%s:%d%s\n", secure ? "wss" : "ws", host, port, path);
    return slot;
}

int platform_ws_state(int h) {
    if (h < 0 || h >= WS_MAX) return 2;
    return s_ws[h].used ? s_ws[h].state : 2;
}

int platform_ws_send(int h, const uint8_t* data, int len, int binary) {
    if (h < 0 || h >= WS_MAX) return -1;
    WsBare* c = &s_ws[h];
    if (!c->used || c->state != 1 || !data || len <= 0) return -1;
    uint8_t* frame = (uint8_t*)kalloc((uint32_t)len + 16);
    if (!frame) return -1;
    uint8_t op = binary ? nefu::netproto::WS_OP_BINARY : nefu::netproto::WS_OP_TEXT;
    int fn = nefu::netproto::ws_encode_frame(frame, op, true, data, len, true, &c->seed);
    int rc = c->tls ? nefu::tls::tls13_send(c->tls, frame, fn)
                    : tcp_send(c->fd, frame, fn);
    kfree(frame);
    return rc == fn ? len : -1;
}

int platform_ws_peek(int h, int* out_binary) {
    if (h < 0 || h >= WS_MAX) return -1;
    WsBare* c = &s_ws[h];
    if (!c->used) return -1;
    if (c->state == 2) {
        if (c->cur_valid) { if (out_binary) *out_binary = c->cur_bin; return c->cur_len; }
        return -1;
    }
    ws_pump(c);
    if (!c->cur_valid && c->qn > 0) {
        int idx = c->qhead;
        c->cur = c->qbuf[idx]; c->cur_len = c->qlen[idx]; c->cur_bin = c->qbin[idx];
        c->qhead = (c->qhead + 1) % WsBare::QN; c->qn--;
        c->cur_off = 0; c->cur_valid = true;
    }
    if (c->cur_valid) { if (out_binary) *out_binary = c->cur_bin; return c->cur_len; }
    return 0;
}

int platform_ws_read(int h, uint8_t* buf, int cap) {
    if (h < 0 || h >= WS_MAX) return 0;
    WsBare* c = &s_ws[h];
    if (!c->used || !c->cur_valid || !buf || cap <= 0) return 0;
    int n = c->cur_len - c->cur_off;
    if (n > cap) n = cap;
    if (n > 0) memcpy(buf, c->cur + c->cur_off, (uint32_t)n);
    c->cur_off += n;
    if (c->cur_off >= c->cur_len) {
        if (c->cur) kfree(c->cur);
        c->cur = 0; c->cur_len = c->cur_off = 0; c->cur_valid = false;
    }
    return n;
}

void platform_ws_close(int h) {
    if (h < 0 || h >= WS_MAX) return;
    WsBare* c = &s_ws[h];
    if (!c->used) return;
    if (c->state == 1) {
        uint8_t cb[128];
        int cn = nefu::netproto::ws_build_close(cb, 1000, "bye", true, &c->seed);
        if (c->tls) nefu::tls::tls13_send(c->tls, cb, cn);
        else        tcp_send(c->fd, cb, cn);
    }
    if (c->tls) { nefu::tls::tls13_close(c->tls); c->tls = 0; }
    if (c->fd >= 0) { tcp_close(c->fd); c->fd = -1; }
    for (int i = 0; i < c->qn; i++) {
        int idx = (c->qhead + i) % WsBare::QN;
        if (c->qbuf[idx]) kfree(c->qbuf[idx]);
    }
    c->qn = 0; c->qhead = 0;
    if (c->cur) { kfree(c->cur); c->cur = 0; }
    c->cur_valid = false; c->cur_len = c->cur_off = 0;
    if (c->rx)    { kfree(c->rx);    c->rx = 0; } c->rx_len = c->rx_cap = 0;
    if (c->frag)  { kfree(c->frag);  c->frag = 0; } c->frag_len = c->frag_cap = 0;
    c->state = 2; c->used = false;
}

// Local file read (bare): only NVFS paths would make sense; not wired.
bool platform_read_file(const char* path, uint8_t** out, uint32_t* out_size) {
    (void)path;
    if (out) *out = 0;
    if (out_size) *out_size = 0;
    return false;
}

// ===================== disk / ATA probe =====================
// Minimal ATA IDENTIFY over the legacy controllers (no DMA, PIO only).
// Returns the number of drives found. Safe on real hardware and QEMU.
static inline uint8_t inb_p(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline uint16_t inw_p(uint16_t port) {
    uint16_t v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outb_p(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline void outw_p(uint16_t port, uint16_t v) {
    __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(port));
}

static bool ata_wait_not_busy(uint16_t base, int timeout_loops) {
    for (int i = 0; i < timeout_loops; i++) {
        if (!(inb_p(base + 7) & 0x80)) return true;   // BSY clear
    }
    return false;
}

static int ata_identify(uint16_t base, uint8_t devsel, char* model, int model_sz,
                        uint64_t* sectors_out, bool* removable_out) {
    // devsel: 0xA0 = master, 0xB0 = slave
    // ATA drives answer IDENTIFY DEVICE (0xEC); ATAPI (CD-ROM) answers
    // IDENTIFY PACKET DEVICE (0xA1) and raises the error bit for 0xEC.
    static const uint8_t cmds[2] = { 0xEC, 0xA1 };
    for (int attempt = 0; attempt < 2; attempt++) {
        outb_p(base + 6, devsel);
        outb_p(base + 2, 0);
        outb_p(base + 3, 0);
        outb_p(base + 4, 0);
        outb_p(base + 5, 0);
        outb_p(base + 7, cmds[attempt]);
        if (!ata_wait_not_busy(base, 200000)) continue;
        uint8_t st = inb_p(base + 7);
        if (st == 0 || (st & 1)) continue;            // no device / error -> try next cmd
        if (!(st & 0x40)) continue;                    // DRDY not set
        int i = 0;
        while (i < 200000) {
            st = inb_p(base + 7);
            if (st & 1) break;                          // error
            if (st & 8) break;                          // DRQ
            i++;
        }
        if (!(st & 8)) continue;
        uint16_t id[256];
        for (int j = 0; j < 256; j++) id[j] = inw_p(base);
        if (removable_out) *removable_out = (id[0] & 0x0080) != 0;
        if (model && model_sz > 0) {
            int m = 0;
            for (int w = 27; w <= 46 && m < model_sz - 1; w++) {
                char c1 = (char)(id[w] >> 8);
                char c0 = (char)(id[w] & 0xFF);
                if (c1 != ' ') model[m++] = c1;
                if (c0 != ' ' && m < model_sz - 1) model[m++] = c0;
            }
            model[m] = 0;
        }
        uint64_t lba48 = (uint64_t)id[100] | ((uint64_t)id[101] << 16) |
                         ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48);
        uint64_t lba28 = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
        if (sectors_out) *sectors_out = (lba48 != 0) ? lba48 : lba28;
        return 1;
    }
    return 0;
}

int platform_disk_scan(DiskInfo* list, int max) {
    if (!list || max <= 0) return 0;
    int n = 0;
    struct Chan { uint16_t base; const char* dev; } chans[] = {
        { 0x1F0, "sda" }, { 0x170, "sdb" },
    };
    for (unsigned c = 0; c < 2 && n < max; c++) {
        for (int sel = 0; sel < 2 && n < max; sel++) {
            DiskInfo d;
            d.name[0] = 0; d.model[0] = 0; d.sectors = 0; d.removable = false;
            // name: sda/sdb (primary), sdc/sdd (secondary)
            char nm[16];
            ksprintf(nm, sizeof(nm), "sd%c", 'a' + (int)(c * 2 + sel));
            uint64_t sectors = 0;
            bool rem = false;
            if (ata_identify(chans[c].base, (uint8_t)(sel ? 0xB0 : 0xA0),
                             d.model, sizeof(d.model), &sectors, &rem)) {
                // de-dup: an empty slave slot can echo the master's identify
                // response on some emulated controllers
                bool dup = false;
                for (int k = 0; k < n; k++) {
                    if (strcmp(list[k].model, d.model) == 0 && list[k].sectors == sectors) { dup = true; break; }
                }
                if (dup) continue;
                ksprintf(d.name, sizeof(d.name), "%s", nm);
                d.sectors = sectors;
                d.removable = rem;
                if (d.model[0] == 0) ksprintf(d.model, sizeof(d.model), "ATA device");
                list[n++] = d;
            }
        }
    }
    return n;
}

// ===================== NVFS persistence (ATA PIO) =====================
// PIO 28-bit LBA sector I/O used to persist the NVFS image so settings,
// the firstboot marker and user files survive reboots (task: real save).
// Layout: the NVFS image starts at sector 0 and is as long as the drive itself
// (capped at FS_MAX_IMAGE). No separate size sector is needed — the superblock
// at offset 0 identifies a valid image, and NVFS is a fixed 4 MiB, which is
// exactly what a 4 MiB drive holds. (An earlier "size word in sector 0, image
// from sector 1" layout could never fit a 4 MiB image on a 4 MiB drive.)
#define FS_IMAGE_LBA 0u
#define FS_MAX_IMAGE  (64u * 1024u * 1024u)

static uint16_t s_fs_base = 0;
static uint8_t  s_fs_dev  = 0;

static bool ata_wait_drq(uint16_t base, int timeout_loops) {
    for (int i = 0; i < timeout_loops; i++) {
        uint8_t st = inb_p(base + 7);
        if (st & 1) return false;          // error bit
        if (st & 8) return true;           // DRQ
    }
    return false;
}

static bool ata_pio_read(uint16_t base, uint8_t devsel, uint32_t lba,
                         uint8_t* buf, uint32_t sectors) {
    while (sectors) {
        uint32_t cnt = sectors > 255 ? 255 : sectors;
        outb_p(base + 6, (uint8_t)(devsel | ((lba >> 24) & 0x0F)));
        outb_p(base + 2, (uint8_t)cnt);
        outb_p(base + 3, (uint8_t)(lba & 0xFF));
        outb_p(base + 4, (uint8_t)((lba >> 8) & 0xFF));
        outb_p(base + 5, (uint8_t)((lba >> 16) & 0xFF));
        outb_p(base + 7, 0x20);            // READ SECTORS (28-bit)
        for (uint32_t s = 0; s < cnt; s++) {
            if (!ata_wait_not_busy(base, 400000)) return false;
            if (!ata_wait_drq(base, 400000)) return false;
            uint16_t* w = (uint16_t*)(buf + (size_t)s * 512u);
            for (int j = 0; j < 256; j++) w[j] = inw_p(base);
        }
        if (!ata_wait_not_busy(base, 400000)) return false;
        buf += (size_t)cnt * 512u;
        lba += cnt;
        sectors -= cnt;
    }
    return true;
}

static bool ata_pio_write(uint16_t base, uint8_t devsel, uint32_t lba,
                          const uint8_t* buf, uint32_t sectors) {
    while (sectors) {
        uint32_t cnt = sectors > 255 ? 255 : sectors;
        outb_p(base + 6, (uint8_t)(devsel | ((lba >> 24) & 0x0F)));
        outb_p(base + 2, (uint8_t)cnt);
        outb_p(base + 3, (uint8_t)(lba & 0xFF));
        outb_p(base + 4, (uint8_t)((lba >> 8) & 0xFF));
        outb_p(base + 5, (uint8_t)((lba >> 16) & 0xFF));
        outb_p(base + 7, 0x30);            // WRITE SECTORS (28-bit)
        for (uint32_t s = 0; s < cnt; s++) {
            if (!ata_wait_not_busy(base, 400000)) return false;
            if (!ata_wait_drq(base, 400000)) return false;
            const uint16_t* w = (const uint16_t*)(buf + (size_t)s * 512u);
            for (int j = 0; j < 256; j++) outw_p(base, w[j]);
        }
        if (!ata_wait_not_busy(base, 400000)) return false;
        buf += (size_t)cnt * 512u;
        lba += cnt;
        sectors -= cnt;
    }
    return true;
}

static uint32_t s_fs_sectors = 0;   // capacity of the drive holding the image

static bool fs_find_disk(uint16_t* base_out, uint8_t* dev_out) {
    static const uint16_t bases[2] = { 0x1F0, 0x170 };
    for (unsigned c = 0; c < 2; c++) {
        for (int sel = 0; sel < 2; sel++) {
            char model[48];
            uint64_t sectors = 0;
            bool rem = false;
            if (ata_identify(bases[c], (uint8_t)(sel ? 0xB0 : 0xA0),
                             model, sizeof(model), &sectors, &rem)) {
                if (rem || sectors < 1024) continue;   // skip CD-ROM / tiny disks
                *base_out = bases[c];
                // 0xE0/0xF0 select drive 0/1 *and* set the LBA bit (0x40).
                // Without that bit the drive falls back to CHS addressing, and
                // every request silently lands one sector early.
                *dev_out = (uint8_t)(sel ? 0xF0 : 0xE0);
                s_fs_sectors = (sectors > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)sectors;
                return true;
            }
        }
    }
    return false;
}

bool platform_fs_load(uint8_t** out, uint32_t* out_size) {
    if (!out || !out_size) return false;
    *out = 0;
    *out_size = 0;
    uint16_t base;
    uint8_t dev;
    if (!fs_find_disk(&base, &dev)) { klogf("nvfs: no ATA disk for image load\n"); return false; }
    // Cheap "is there an image here?" check before allocating 4 MiB: read the
    // first sector and compare the superblock magic (0x4E564653 little endian).
    uint8_t first[512];
    memset(first, 0, sizeof(first));
    if (!ata_pio_read(base, dev, FS_IMAGE_LBA, first, 1)) { klogf("nvfs: image sector 0 read failed\n"); return false; }
    static const uint8_t kMagic[4] = { 0x53, 0x46, 0x56, 0x4E };
    if (first[0] != kMagic[0] || first[1] != kMagic[1] ||
        first[2] != kMagic[2] || first[3] != kMagic[3]) return false;   // blank / foreign disk
    uint32_t size = FS_MAX_IMAGE;
    uint32_t sectors = size / 512u;
    if (s_fs_sectors && sectors > s_fs_sectors) {
        sectors = s_fs_sectors;              // drive smaller than the image
        size = sectors * 512u;
    }
    uint8_t* img = (uint8_t*)kalloc(size);
    if (!img) { klogf("nvfs: image alloc failed (%u bytes)\n", (unsigned)size); return false; }
    uint8_t* p = img;
    uint32_t done = 0;
    uint32_t left = sectors;
    while (left) {
        uint32_t n = left > 64 ? 64 : left;
        uint8_t* tmp = (uint8_t*)kalloc(n * 512u);
        if (!tmp) { kfree(img); return false; }
        if (!ata_pio_read(base, dev, FS_IMAGE_LBA + (sectors - left), tmp, n)) {
            klogf("nvfs: image read failed at lba %u\n", (unsigned)(FS_IMAGE_LBA + (sectors - left)));
            kfree(tmp); kfree(img); return false;
        }
        uint32_t take = (n * 512u > size - done) ? (size - done) : n * 512u;
        memcpy(p, tmp, take);
        p += take;
        done += take;
        kfree(tmp);
        left -= n;
    }
    s_fs_base = base;
    s_fs_dev = dev;
    *out = img;
    *out_size = size;
    klogf("nvfs: loaded %u bytes from ATA disk\n", (unsigned)size);
    return true;
}

// The bare backend has no LLM model file/disk; the chatbot falls back to its
// built-in rule table (see core/apps/chatbot.cpp).
bool platform_llm_load(uint8_t** out, uint32_t* out_size) {
    if (out) *out = 0;
    if (out_size) *out_size = 0;
    return false;
}

void platform_fs_save(const uint8_t* data, uint32_t size) {
    if (!data || size == 0 || size > FS_MAX_IMAGE) return;
    uint16_t base;
    uint8_t dev;
    if (s_fs_base) {
        base = s_fs_base;
        dev = s_fs_dev;
    } else if (!fs_find_disk(&base, &dev)) {
        return;
    }
    uint32_t sectors = (size + 511u) / 512u;
    if (s_fs_sectors && sectors > s_fs_sectors) sectors = s_fs_sectors;
    const uint8_t* p = data;
    uint32_t left = sectors;
    uint32_t done = 0;
    while (left) {
        uint32_t n = left > 64 ? 64 : left;
        uint8_t* tmp = (uint8_t*)kalloc(n * 512u);
        if (!tmp) return;
        memset(tmp, 0, n * 512u);
        uint32_t take = (n * 512u > size - done) ? (size - done) : n * 512u;
        memcpy(tmp, p, take);
        if (!ata_pio_write(base, dev, FS_IMAGE_LBA + (sectors - left), tmp, n)) {
            kfree(tmp);
            return;
        }
        p += take;
        done += take;
        kfree(tmp);
        left -= n;
    }
    klogf("nvfs: saved %u bytes to ATA disk\n", (unsigned)size);
}

// Incremental write-back: four commits per second are only affordable because
// just the blocks that actually changed are pushed to the disk. The image
// lives at sector 0 with the superblock up front, so a block b of the image is
// drive sector b; no header rewrite is required and nothing is lost if the
// drive is exactly as large as the image.
bool platform_fs_save_range(const uint8_t* data, uint32_t total_size,
                            uint32_t first_block, uint32_t count) {
    if (!data || total_size == 0 || total_size > FS_MAX_IMAGE || count == 0) return false;
    uint16_t base;
    uint8_t dev;
    if (s_fs_base) {
        base = s_fs_base;
        dev = s_fs_dev;
    } else if (!fs_find_disk(&base, &dev)) {
        return false;
    }
    uint32_t off = first_block * 512u;
    if (off >= total_size) return true;
    uint32_t sectors = count;
    uint32_t max_sectors = (total_size - off + 511u) / 512u;
    if (sectors > max_sectors) sectors = max_sectors;
    uint32_t lba = FS_IMAGE_LBA + first_block;
    if (s_fs_sectors && lba + sectors > s_fs_sectors) {
        if (lba >= s_fs_sectors) return false;
        sectors = s_fs_sectors - lba;
    }
    const uint8_t* p = data + off;
    while (sectors) {
        uint32_t n = sectors > 64 ? 64 : sectors;
        if (!ata_pio_write(base, dev, lba, p, n)) return false;
        p += (size_t)n * 512u;
        lba += n;
        sectors -= n;
    }
    return true;
}

// Bare-metal cadence: four commits per second (250 ms). This is the whole
// point of the incremental write-back - the image is tiny, the dirty set
// usually is not.
uint32_t platform_fs_flush_interval_ms() { return 250u; }


// ---- threading (bare: cooperative, run synchronously) ----
void* platform_thread_create_stk(void (*func)(void*), void* arg, uint32_t) {
    func(arg);
    return (void*)1;
}

void* platform_thread_create(void (*func)(void*), void* arg) {
    func(arg);   // bare metal: run synchronously (no preemptive scheduler yet)
    return (void*)1;
}

void platform_thread_sleep(uint32_t ms) {
    uint32_t start = platform_tick_ms();
    while (platform_tick_ms() - start < ms) { /* busy wait */ }
}

// ---- audio: real Sound Blaster 16 driver (DSP + 8237 DMA) ----
// QEMU: -soundhw sb16  ->  base 0x220, IRQ5, DMA1 ch1 (8-bit),
// DMA5 (16-bit). The driver probes the DSP, converts any PCM WAV to
// 16-bit stereo frames in a fixed DMA-safe buffer below the 16MB ISA
// boundary and plays it with a 16-bit single-cycle DMA transfer.
#define SB_BASE     0x220
#define SB_RESET    (SB_BASE + 6)    // 0x226 DSP reset
#define SB_READ     (SB_BASE + 0xA)  // 0x22A DSP read data
#define SB_WRITE    (SB_BASE + 0xC)  // 0x22C DSP write command
#define SB_STATUS   (SB_BASE + 0xE)  // 0x22E DSP status (bit7 = data ready)

#define DMA2_ADDR1  0xC4             // 16-bit controller ch1 address
#define DMA2_CNT1   0xC6             // 16-bit controller ch1 count (words-1)
#define DMA2_PAGE1  0x8B             // ch1 page register
#define DMA2_MODE   0xD6
#define DMA2_MASK   0xD4
#define DMA2_CLRFF  0xD8
#define DMA2_MASTER 0xDA

#define AUDIO_DMA_BUF  0x0E00000u    // fixed 14MB buffer (ISA DMA < 16MB)
#define AUDIO_DMA_MAX  (512u * 1024u) // max 128k stereo frames

static bool s_sb16_ok = false;
static bool s_sb16_probed = false;

static void sb_dsp_write(uint8_t v) {
    for (int i = 0; i < 300000; i++) {
        if (!(inb_p(SB_WRITE) & 0x80)) break;   // bit7 = write buffer full
    }
    outb_p(SB_WRITE, v);
}

static int sb_dsp_read(void) {
    for (int i = 0; i < 300000; i++) {
        if (inb_p(SB_STATUS) & 0x80) return inb_p(SB_READ);
    }
    return -1;
}

static bool sb_dsp_reset(void) {
    outb_p(SB_RESET, 1);
    for (int i = 0; i < 32; i++) inb_p(SB_STATUS);   // ~3us settle delay
    outb_p(SB_RESET, 0);
    return sb_dsp_read() == 0xAA;                    // DSP ready signature
}

static bool sb_audio_init(void) {
    if (s_sb16_probed) return s_sb16_ok;
    s_sb16_probed = true;
    s_sb16_ok = sb_dsp_reset();
    return s_sb16_ok;
}

static bool sb_dma5_play(const void* buf, uint32_t bytes) {
    if (!buf || bytes < 2 || (bytes & 1)) return false;
    uint32_t addr = (uint32_t)(uintptr_t)buf;
    if (addr > 0xFFFFFFu || addr + bytes > 0x1000000u) return false; // ISA limit
    uint16_t words = (uint16_t)(bytes / 2 - 1);
    outb_p(DMA2_MASTER, 0);
    outb_p(DMA2_PAGE1, (uint8_t)(addr >> 16));
    outb_p(DMA2_CLRFF, 0);
    outb_p(DMA2_ADDR1, (uint8_t)(addr & 0xFF));
    outb_p(DMA2_ADDR1, (uint8_t)((addr >> 8) & 0xFF));
    outb_p(DMA2_CNT1, (uint8_t)(words & 0xFF));
    outb_p(DMA2_CNT1, (uint8_t)((words >> 8) & 0xFF));
    outb_p(DMA2_MODE, 0xC9);         // 16-bit ch1, single, write, increment
    outb_p(DMA2_MASK, 0x01);         // unmask ch1
    sb_dsp_write(0xB0);              // 16-bit single-cycle DAC output
    sb_dsp_write((uint8_t)((words >> 8) & 0xFF));
    sb_dsp_write((uint8_t)(words & 0xFF));
    return true;
}

// ---- WAV parsing (RIFF/WAVE, uncompressed PCM) ----
struct WavInfo {
    uint32_t rate;
    int channels;
    int bits;
    const uint8_t* data;
    uint32_t len;
};

static bool wav_parse(const uint8_t* w, uint32_t sz, WavInfo& o) {
    o.rate = 0; o.channels = 0; o.bits = 0; o.data = 0; o.len = 0;
    if (sz < 12 || w[0] != 'R' || w[1] != 'I' || w[2] != 'F' || w[3] != 'F') return false;
    if (w[8] != 'W' || w[9] != 'A' || w[10] != 'V' || w[11] != 'E') return false;
    uint32_t i = 12;
    while (i + 8 <= sz) {
        uint32_t id = (uint32_t)w[i] | ((uint32_t)w[i + 1] << 8) |
                      ((uint32_t)w[i + 2] << 16) | ((uint32_t)w[i + 3] << 24);
        uint32_t clen = (uint32_t)w[i + 4] | ((uint32_t)w[i + 5] << 8) |
                        ((uint32_t)w[i + 6] << 16) | ((uint32_t)w[i + 7] << 24);
        uint32_t body = i + 8;
        if (body + clen > sz) break;
        if (id == 0x20746D66u && clen >= 16) {        // "fmt "
            o.bits     = (int)(w[body + 14] | (w[body + 15] << 8));
            o.channels = (int)(w[body + 2] | (w[body + 3] << 8));
            o.rate     = (uint32_t)w[body + 4] | ((uint32_t)w[body + 5] << 8) |
                         ((uint32_t)w[body + 6] << 16) | ((uint32_t)w[body + 7] << 24);
        } else if (id == 0x61746164u) {               // "data"
            o.data = w + body;
            o.len = clen;
        }
        i = body + clen + (clen & 1);
    }
    return o.data && o.len > 0 && o.rate > 0 &&
           (o.channels == 1 || o.channels == 2) && (o.bits == 8 || o.bits == 16);
}

// Convert any PCM WAV (8/16-bit, mono/stereo) to 16-bit stereo frames in
// the fixed DMA-safe buffer. Returns frame count and sample rate.
static bool wav_to_pcm16_stereo(const WavInfo& w, uint32_t& out_frames, uint32_t& out_rate) {
    uint8_t* dst = (uint8_t*)AUDIO_DMA_BUF;
    uint32_t bps = (uint32_t)w.bits / 8;
    uint32_t frame_bytes = (uint32_t)w.channels * bps;
    if (frame_bytes == 0) return false;
    uint32_t frames = w.len / frame_bytes;
    if (frames > AUDIO_DMA_MAX / 4) frames = AUDIO_DMA_MAX / 4;
    int16_t* p = (int16_t*)dst;
    for (uint32_t i = 0; i < frames; i++) {
        const uint8_t* s = w.data + (size_t)i * frame_bytes;
        int16_t l, r;
        if (w.bits == 8) {
            if (w.channels == 1) {
                l = r = (int16_t)(((int32_t)s[0] - 128) << 8);
            } else {
                l = (int16_t)(((int32_t)s[0] - 128) << 8);
                r = (int16_t)(((int32_t)s[1] - 128) << 8);
            }
        } else {
            if (w.channels == 1) {
                l = r = (int16_t)(int16_t)(s[0] | (s[1] << 8));
            } else {
                l = (int16_t)(s[0] | (s[1] << 8));
                r = (int16_t)(s[2] | (s[3] << 8));
            }
        }
        p[i * 2] = l;
        p[i * 2 + 1] = r;
    }
    out_frames = frames;
    out_rate = w.rate;
    return true;
}

bool platform_audio_available() {
    if (nefu::audio::engine_available()) return true;
    return sb_audio_init();
}

bool platform_play_wav_mem(const uint8_t* data, uint32_t size) {
    if (!data || size < 44) return false;
    // prefer the real PCI sound card (AC'97 / HD Audio) via the integer engine
    if (nefu::audio::engine_play_wav_mem(data, size)) return true;
    if (!sb_audio_init()) return false;
    WavInfo w;
    if (!wav_parse(data, size, w)) return false;
    uint32_t frames = 0, rate = 0;
    if (!wav_to_pcm16_stereo(w, frames, rate)) return false;
    if (frames == 0 || rate == 0) return false;
    uint32_t bytes = frames * 4;
    sb_dsp_write(0x41);                // set output sample rate
    sb_dsp_write((uint8_t)((rate >> 8) & 0xFF));
    sb_dsp_write((uint8_t)(rate & 0xFF));
    sb_dsp_write(0x48);                // set master volume (~2/3)
    sb_dsp_write(0xA0);
    sb_dsp_write(0xA0);
    klogf("audio: sb16 play %u frames @ %u Hz stereo\n", frames, rate);
    return sb_dma5_play((void*)AUDIO_DMA_BUF, bytes);
}

bool platform_play_wav(const char* path) {
    // bare: resolve via the VFS through the shared kernel (defined in core)
    return platform_play_wav_path(path);
}

void platform_stop_sound() {
    if (!s_sb16_probed || !s_sb16_ok) return;
    sb_dsp_write(0xD5);                // pause 16-bit DAC
    sb_dsp_write(0xD4);                // speaker off (16-bit)
    outb_p(DMA2_MASK, 0x05);           // mask DMA ch1 to halt transfer
}

// ===================== media (bare: real WAV playback via the audio engine) =====================
// The bare kernel has no Media Foundation; the integer audio engine plays WAV
// files straight through the real PCI sound card (AC'97 / HD Audio).  VFS
// paths are resolved inside the engine.
bool platform_media_available() { return nefu::audio::engine_available(); }
bool platform_media_open(const char* host_path, bool audio_only) {
    (void)audio_only;
    if (!host_path || !host_path[0]) return false;
    return nefu::audio::media_open_path(host_path);
}
void platform_media_close() { nefu::audio::media_close(); }
bool platform_media_play() { return nefu::audio::media_play(); }
bool platform_media_pause() { nefu::audio::media_pause(); return true; }
void platform_media_stop() { nefu::audio::media_stop(); }
bool platform_media_seek_sec(int sec) {
    if (sec < 0) sec = 0;
    return nefu::audio::media_seek_sec(sec);
}
int  platform_media_position_sec() { return nefu::audio::media_position_sec(); }
int  platform_media_duration_sec() { return nefu::audio::media_duration_sec(); }
void platform_media_set_volume(int percent) { nefu::audio::media_set_volume(percent); }
bool platform_media_has_video() { return false; }
bool platform_media_frame_info(int* w, int* h) { (void)w; (void)h; return false; }
bool platform_media_grab_frame(uint8_t* out_rgba) { (void)out_rgba; return false; }
bool platform_host_file_dialog(char* out_path, int max, const char* filter_desc, const char* filter_pattern) {
    (void)out_path; (void)max; (void)filter_desc; (void)filter_pattern;
    return false;
}

} // namespace nefu

