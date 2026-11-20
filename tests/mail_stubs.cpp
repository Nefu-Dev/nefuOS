/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// mail_logic_test stubs: minimal host-side implementations of the platform
// + kernel entry points that email.h / vfs.cpp touch in the logic paths.
#include <cstdlib>
#include "../core/platform.h"
#include "../core/gui/wm.h"
#include "../core/vfs/vfs.h"
#include "../core/net/net.h"
#include "../core/net/tls13.h"

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

// ---- bare TCP (never exercised by the logic tests) ----
int tcp_connect(uint32_t ip, uint16_t port, int timeout_ms) {
    (void)ip; (void)port; (void)timeout_ms;
    return -1;
}
int tcp_send(int fd, const void* data, int len) {
    (void)fd; (void)data; (void)len;
    return -1;
}
int tcp_recv(int fd, void* buf, int maxlen, int timeout_ms) {
    (void)fd; (void)buf; (void)maxlen; (void)timeout_ms;
    return -1;
}
void tcp_close(int fd) { (void)fd; }

// ---- TLS 1.3 (never exercised by the logic tests) ----
namespace tls {
struct Tls13Conn;
Tls13Conn* tls13_connect(tls_recv_fn rf, tls_send_fn sf, void* ctx,
                         const char* hostname, char* err, int errsz) {
    (void)rf; (void)sf; (void)ctx; (void)hostname;
    if (err && errsz > 0) err[0] = 0;
    return 0;
}
int tls13_send(Tls13Conn* c, const uint8_t* data, int len) {
    (void)c; (void)data; (void)len;
    return -1;
}
int tls13_recv(Tls13Conn* c, uint8_t* buf, int maxlen, int timeout_ms) {
    (void)c; (void)buf; (void)maxlen; (void)timeout_ms;
    return -1;
}
void tls13_close(Tls13Conn* c) { (void)c; }
const char* tls13_alpn(Tls13Conn* c) { (void)c; return ""; }
} // namespace tls

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

