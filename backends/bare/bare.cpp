// nefuOS （x86_64 ，QEMU/）
// platform implementation：LFB 、PIT 、PS/2 +、RTC、UART debug、
#include <stdint.h>
#include <stddef.h>
#include "../../core/klib/klib.h"
#include "../../core/platform.h"
#include "../../core/gui/gfx.h"   // full Surface definition

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
#define ARENA_BASE 0x400000u
#define ARENA_SIZE (64u * 1024u * 1024u)
struct Blk { uint32_t size; Blk* next; };   // 8 bytes， 16
static uint32_t s_arena_next = ARENA_BASE;
static Blk* s_free = 0;

static void* alloc_from_bump(uint32_t sz) {
    uint32_t need = (sz + 15) & ~15u;
    uint32_t cur = s_arena_next;
    if (cur + need + 16 > ARENA_BASE + ARENA_SIZE) return 0;
    s_arena_next = cur + need + 16;
    Blk* b = (Blk*)cur;
    b->size = need;
    return (void*)(b + 1);
}

void* kalloc(size_t sz) {
    if (sz == 0) sz = 8;
    uint32_t need = ((uint32_t)sz + 15) & ~15u;

    Blk** pp = &s_free;
    while (*pp) {
        if ((*pp)->size >= need) {
            Blk* b = *pp;
            *pp = b->next;
            return (void*)(b + 1);
        }
        pp = &(*pp)->next;
    }
    return alloc_from_bump(need);
}

void kfree(void* p) {
    if (!p) return;
    Blk* b = (Blk*)p - 1;
    b->next = s_free;
    s_free = b;
}

// ===================== screen =====================
static Screen s_screen;
static uint32_t s_lfb = 0;

Screen* platform_screen() { return &s_screen; }
void platform_present() {}

// ===================== hour =====================
static volatile uint32_t s_ticks = 0;   // 100Hz
static volatile uint32_t s_tick_ms = 0;

uint32_t platform_tick_ms() { return s_tick_ms; }

// bare kernel has no OS image decoder; caller falls back to built-in bmp/ppm
bool platform_decode_image(const uint8_t* data, uint32_t size, Surface& out) {
    (void)data; (void)size; out.addr = 0; return false;
}

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
#define KQ_MAX 64
struct KEvent { int kc; char ascii; bool down; };
static volatile int s_kq_head = 0, s_kq_tail = 0;
static KEvent s_kq[KQ_MAX];
static void kq_push(int kc, char ac, bool down) {
    int n = (s_kq_tail + 1) % KQ_MAX;
    if (n == s_kq_head) return;
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

// （set 1）：kc!=0 ，
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
    { 0x52, 0, 0, 0 },               // Ins（ignore）
    { 0x53, KEY_DEL,     0, 0 },
    { 0x3B, KEY_F1, 0, 0 }, { 0x3C, KEY_F2, 0, 0 }, { 0x3D, KEY_F3, 0, 0 },
    { 0x3E, KEY_F4, 0, 0 }, { 0x3F, KEY_F5, 0, 0 }, { 0x40, KEY_F6, 0, 0 },
    { 0x41, KEY_F7, 0, 0 }, { 0x42, KEY_F8, 0, 0 }, { 0x43, KEY_F9, 0, 0 },
    { 0x44, KEY_F10, 0, 0 }, { 0x57, KEY_F11, 0, 0 }, { 0x58, KEY_F12, 0, 0 },

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
    bool down = !(sc & 0x80);
    uint8_t code = sc & 0x7F;
    if (code == 0x2A || code == 0x36) { s_shift = down; return; }
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
            if (r.kc != 0) { kq_push(r.kc, 0, down); return; }
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
    s_mb = (uint8_t)(((b0 & 1) ? 0 : 1) | ((b0 & 2) ? 0 : 2) | ((b0 & 4) ? 0 : 4));
    s_mouse_dirty = true;
}

__attribute__((interrupt)) static void irq0_handler(void* frame) {
    (void)frame;
    s_ticks++;
    s_tick_ms = s_ticks * 10;
    pic_eoi(0);
}
__attribute__((interrupt)) static void irq1_handler(void* frame) {
    (void)frame;
    uint8_t sc = inb(0x60);
    kbd_scancode(sc);
    pic_eoi(1);
}
__attribute__((interrupt)) static void irq12_handler(void* frame) {
    (void)frame;
    mouse_byte(inb(0x60));
    pic_eoi(12);
}
__attribute__((interrupt)) static void spurious_handler(void* frame) { (void)frame; }

static void idt_init() {
    // spurious， IRQ
    for (int i = 0; i < 256; i++) idt_set(i, (void*)spurious_handler);
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
    // ， IRQ12
    outb(0x64, 0x20); io_wait();
    uint8_t cb = inb(0x60);
    cb |= 0x02;        // enable aux IRQ
    cb &= ~0x20;       // enable aux clock
    cb &= ~0x10;       // enable keyboard IRQ
    outb(0x64, 0x60); io_wait();
    outb(0x60, cb); io_wait();

    outb(0x64, 0xD4); io_wait();
    outb(0x60, 0xF4); io_wait();
    // ACK
    for (int i = 0; i < 4; i++) inb(0x60);
}

// ===================== RTC =====================
static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}
static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v & 0x0F) + (v >> 4) * 10); }

uint32_t platform_seconds_of_day() {
    uint8_t s = cmos_read(0x00);
    uint8_t m = cmos_read(0x02);
    uint8_t h = cmos_read(0x04);
    uint8_t b = cmos_read(0x0B);
    if (!(b & 0x04)) { s = bcd2bin(s); m = bcd2bin(m); h = bcd2bin(h); }
    if (!(b & 0x02)) {            // 12
        bool pm = (h & 0x80) != 0;
        h &= 0x7F;
        if (h >= 12) h = (uint8_t)(h - 12);
        if (pm) h += 12;
    }
    return (uint32_t)h * 3600u + (uint32_t)m * 60u + (uint32_t)s;
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

void platform_mem_stats(uint32_t* used, uint32_t* total) {
    *used = s_arena_next - ARENA_BASE;
    *total = ARENA_SIZE;
}

const char* platform_name() { return "bare (x86_64)"; }

bool platform_fs_load(uint8_t** out, uint32_t* out_size) {
    (void)out; (void)out_size;
    return false;   // ，
}
void platform_fs_save(const uint8_t* data, uint32_t size) { (void)data; (void)size; }

// ===================== =====================
extern "C" void nefuos_kernel_main(void* info) {
    __asm__ volatile("movw $0xE9, %%dx; movb $'1', %%al; outb %%al, %%dx" ::: "dx", "ax");
    uint32_t* bi = (uint32_t*)info;
    s_lfb = bi[0];
    s_screen.addr = (uint8_t*)s_lfb;
    s_screen.width = (int)bi[1];
    s_screen.height = (int)bi[2];
    s_screen.pitch = (int)bi[3];
    __asm__ volatile("movw $0xE9, %%dx; movb $'2', %%al; outb %%al, %%dx" ::: "dx", "ax");

    uart_init();
    platform_dbg("\nnefuOS bare kernel: LFB=0x");
    for (int i = 28; i >= 0; i -= 4) {
        int d = (int)((s_lfb >> i) & 0xF);
        uart_putc((char)(d < 10 ? '0' + d : 'A' + d - 10));
    }
    __asm__ volatile("movw $0xE9, %%dx; movb $'X', %%al; outb %%al, %%dx" ::: "dx", "ax");
    {
        char tmp[48];
        ksprintf(tmp, sizeof(tmp), " screen=%dx%dx32\n", s_screen.width, s_screen.height);
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

    __asm__ volatile("movw $0xE9, %%dx; movb $'C', %%al; outb %%al, %%dx" ::: "dx", "ax");

    __asm__ volatile("movw $0xE9, %%dx; movb $'D', %%al; outb %%al, %%dx" ::: "dx", "ax");
    nefuos_frame();   // manual first frame before interrupts
    __asm__ volatile("movw $0xE9, %%dx; movb $'E', %%al; outb %%al, %%dx" ::: "dx", "ax");

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
            s_my += s_mouse_dy;          // PS/2 Y =
            s_mouse_dx = 0; s_mouse_dy = 0;
            if (s_mx < 0) s_mx = 0;
            if (s_mx >= s_screen.width) s_mx = s_screen.width - 1;
            if (s_my < 0) s_my = 0;
            if (s_my >= s_screen.height) s_my = s_screen.height - 1;
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

// ===================== （bare） =====================
#include "../../core/net/net.h"
namespace nefu {

bool platform_net_get(NetAdapterInfo* out) {
    if (!out) return false;
    out->up = g_net.up;
    for (int i = 0; i < 6; i++) out->mac[i] = g_net.mac[i];
    out->ip = g_net.ip;
    out->gw = g_net.gw;
    out->mask = g_net.netmask;
    strncpy(out->name, "Intel PRO/1000 (e1000)", 48);
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
    (void)url;
    if (out) *out = 0;
    if (out_size) *out_size = 0;
    return false;   // bare uses the in-house TCP stack (IP literals only)
}

} // namespace nefu
