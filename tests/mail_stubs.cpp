// mail_logic_test stubs: minimal host-side implementations of the platform
// + kernel entry points that email.h / vfs.cpp touch in the logic paths.
#include <cstdlib>
#include "../core/platform.h"
#include "../core/gui/wm.h"
#include "../core/vfs/vfs.h"
#include "../core/net/net.h"

namespace nefu {

// ---- memory (malloc-backed like the win32 backend) ----
void* kalloc(size_t sz) {
    if (sz == 0) sz = 8;
    return malloc(sz);
}
void kfree(void* p) { free(p); }
void* krealloc(void* p, size_t sz) {
    if (!p) return malloc(sz);
    return realloc(p, sz);
}

// ---- time ----
static uint32_t s_tick = 1000;
uint32_t platform_tick_ms() { return s_tick += 16; }
bool platform_rtc_date(DateInfo* out) {
    if (!out) return false;
    out->year = 2026; out->month = 9; out->day = 24;
    out->hour = 10; out->min = 5; out->sec = 0;
    out->dow = 4;
    return true;
}
uint32_t platform_seconds_of_day() { return 10 * 3600 + 5 * 60; }
uint32_t nefuos_uptime_ms() { return s_tick; }

// ---- debug ----
void platform_dbg(const char* s) { (void)s; }

// ---- TTF (unused in logic tests; gfx falls back to the bitmap font) ----
bool platform_ttf_text(const char* utf8, int px, int& out_w, int& out_h, uint8_t*& out_rgba) {
    (void)utf8; (void)px; (void)out_w; (void)out_h;
    out_rgba = 0;
    return false;
}
void platform_ttf_free(uint8_t* p) { (void)p; }

// ---- globals used by email.h / vfs ----
WM* g_wm = 0;   // vfs.cpp provides g_vfs; net.cpp provides g_net (not linked here)
NetIf g_net;

// ---- WM stub (open_email is never called in the logic tests) ----
Window* WM::create_window(const char* title, int x, int y, int w, int h, bool fullscreen) {
    (void)title; (void)x; (void)y; (void)w; (void)h; (void)fullscreen;
    return 0;
}

// ---- anything else the linker may pull in ----
void platform_present() {}
void nefuos_tick() {}
void nefuos_frame() {}

} // namespace nefu
