// nvfs_test stubs: minimal host-side implementations of the platform entry
// points that klib / vfs.cpp / nvfs.cpp touch in the logic paths.
#include <cstdlib>
#include "../core/platform.h"
#include "../core/klib/klib.h"

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
uint32_t platform_seconds_of_day() { return 10 * 3600 + 5 * 60; }
uint32_t nefuos_uptime_ms() { return s_tick; }
bool platform_rtc_date(DateInfo* out) {
    if (!out) return false;
    out->year = 2026; out->month = 9; out->day = 24;
    out->hour = 10; out->min = 5; out->sec = 0;
    out->dow = 4;
    return true;
}

// ---- debug ----
void platform_dbg(const char* s) { (void)s; }

// ---- anything else the linker may pull in ----
void platform_present() {}
void nefuos_tick() {}
void nefuos_frame() {}

} // namespace nefu
