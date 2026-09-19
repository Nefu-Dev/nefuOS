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
    b->next = s_free;
    s_free = b;
}

void* krealloc(void* p, size_t sz) {
    if (!p) return kalloc(sz);
    Blk* b = (Blk*)p - 1;
    uint32_t old = b->size;
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
void platform_present() {}

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
    // debug: emit the raw scancode on the debugcon port
    outb(0xE9, 'S');
    static const char hexd[] = "0123456789ABCDEF";
    outb(0xE9, hexd[(sc >> 4) & 0xF]);
    outb(0xE9, hexd[sc & 0xF]);
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
    // Wait for the 0xFA ACK of the enable command (with timeout), so it never
    // leaks into the motion stream.
    for (int i = 0; i < 10000; i++) {
        if (inb(0x64) & 1) { if (inb(0x60) == 0xFA) break; }
        io_wait();
    }
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

bool platform_rtc_date(DateInfo* out) {
    if (!out) return false;
    uint8_t s = cmos_read(0x00);
    uint8_t m = cmos_read(0x02);
    uint8_t h = cmos_read(0x04);
    uint8_t d = cmos_read(0x06);
    uint8_t mo = cmos_read(0x07);
    uint8_t y = cmos_read(0x08);
    uint8_t c = cmos_read(0x09);
    uint8_t wd = cmos_read(0x0A);
    uint8_t b = cmos_read(0x0B);
    if (!(b & 0x04)) {
        s = bcd2bin(s); m = bcd2bin(m); h = bcd2bin(h);
        d = bcd2bin(d); mo = bcd2bin(mo); y = bcd2bin(y); c = bcd2bin(c);
    }
    if (!(b & 0x02)) {            // 12-hour mode
        bool pm = (h & 0x80) != 0;
        h &= 0x7F;
        if (h >= 12) h = (uint8_t)(h - 12);
        if (pm) h += 12;
    }
    if (mo < 1 || mo > 12 || d < 1 || d > 31) return false;
    out->year = (int)c * 100 + (int)y;
    out->month = (int)mo;
    out->day = (int)d;
    out->hour = (int)h;
    out->min = (int)m;
    out->sec = (int)s;
    // CMOS day-of-week: 1 = Sunday .. 7 = Saturday -> 0 = Sunday
    out->dow = (int)((wd == 0) ? 0 : (wd - 1) % 7);
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
    // 8042键盘控制器复位（标准x86重启方式）
    uint8_t good = 0x02;
    while (good & 0x02) good = inb(0x64);
    outb(0x64, 0xFE);  // 脉冲CPU复位线
    for(;;) { __asm__ volatile("hlt"); }
}

void platform_suspend() {
    // 裸机待机：关屏后等待按键唤醒
    Screen* s = platform_screen();
    // 清空屏幕为黑色
    for (int y=0; y<s->height; y++) {
        uint32_t* row = (uint32_t*)(s->addr + y*s->pitch);
        for (int x=0; x<s->width; x++) row[x] = 0;
    }
    // 等待键盘输入唤醒
    for(;;) {
        __asm__ volatile("hlt");
        // 有按键事件就唤醒回到锁屏流程
        if (s_kq_head != s_kq_tail) break;
    }
}

// CPUID指令读取CPU信息
static void cpuid(uint32_t leaf, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
    __asm__ volatile("cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(leaf));
}

bool platform_hw_info(HwInfo* out) {
    memset(out, 0, sizeof(*out));
    // 读CPU型号
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
    // CPU主频从CPUID 0x16读
    cpuid(0x16, &a,&b,&c,&d);
    out->cpu_mhz = a;
    out->cpu_cores = 1;
    // 内存大小从CMOS读（低16MB以上的扩展内存）
    uint16_t mem_kb = cmos_read(0x17) | (cmos_read(0x18) << 8);
    out->mem_total_mb = (ARENA_SIZE / (1024*1024)) + (mem_kb / 1024);
    strncpy(out->bios_vendor, "SeaBIOS/QEMU", 31);
    strncpy(out->bios_version, "1.0.0-nefuOS", 31);
    return true;
}

// UEFI配置存在CMOS/CMOS掉电RAM的0x10-0x30偏移处
bool platform_uefi_load(UefiConfig* out) {
    memset(out, 0, sizeof(*out));
    // 默认值
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
    // 裸机CMOS写入可以扩展，这里直接返回成功
    return true;
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

    // audio: probe the SB16 DSP once so headless runs can verify the driver
    if (sb_audio_init())
        platform_dbg("audio: sb16 detected at 0x220 (DSP 0xAA)\n");
    else
        platform_dbg("audio: no sb16 sound card (run QEMU with -soundhw sb16)\n");

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


// ---- threading (bare: cooperative, run synchronously) ----
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

bool platform_audio_available() { return sb_audio_init(); }

bool platform_play_wav_mem(const uint8_t* data, uint32_t size) {
    if (!data || size < 44) return false;
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

} // namespace nefu
