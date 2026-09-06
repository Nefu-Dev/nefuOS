// nefuOS platform abstraction layer.
// The core only depends on the interfaces declared here; the backends
// (win32 / bare) implement them separately.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {

// image decode: host backend uses the OS decoder (jpg/png/gif/bmp);
// bare backend returns false and the caller falls back to built-in bmp/ppm
struct Surface;
bool platform_decode_image(const uint8_t* data, uint32_t size, struct Surface& out);

// ===================== memory =====================
void* kalloc(size_t sz);
void  kfree(void* p);

// ===================== screen =====================
// Software pixel buffer, 32bpp, little-endian BGRA (uint32 = 0x00RRGGBB)
struct Screen {
    uint8_t* addr;
    int width;
    int height;
    int pitch;      // bytes per row (>= width*4)
};
Screen* platform_screen();
// Host backend blits the buffer to the real screen each frame (bare: no-op)
void platform_present();

// ===================== time =====================
uint32_t platform_tick_ms();        // ms since boot
uint32_t platform_seconds_of_day();
uint32_t nefuos_uptime_ms();        // ms since nefuOS started (bare: uptime)

// ===================== debug output =====================
void platform_dbg(const char* s);

// ===================== core callbacks (called by backends) =====================
void nefuos_handle_key(int keycode, char ascii, bool down);
void nefuos_handle_mouse(int x, int y, uint8_t buttons);
void nefuos_handle_scroll(int delta);
void nefuos_tick();     // periodic heartbeat (clock/animation)
void nefuos_frame();    // redraw every frame
void nefuos_init();     // system init
void nefuos_shutdown(); // cleanup on exit (saves VFS etc.)

// memory stats (for the info display)
void platform_mem_stats(uint32_t* used, uint32_t* total);
const char* platform_name();

// ===================== persistence (host only, bare: no-op) =====================
bool platform_fs_load(uint8_t** out, uint32_t* out_size);
void platform_fs_save(const uint8_t* data, uint32_t size);

// ===================== power off / exit =====================
void platform_poweroff();

// ===================== networking (real adapter info) =====================
// bare: e1000 static config (10.0.2.15/24, gw 10.0.2.2) once net_init ran;
// host: real Windows adapter info via GetAdaptersInfo.
struct NetAdapterInfo {
    bool up;                  // link up?
    uint8_t mac[6];
    uint32_t ip;              // host order
    uint32_t gw;
    uint32_t mask;
    char name[48];            // human readable adapter name
};
// Fills out with the active adapter. Returns false when no adapter is usable.
bool platform_net_get(NetAdapterInfo* out);

// host: real Wi-Fi scan via WlanGetAvailableNetworkList; bare: returns 0
// (e1000 is wired). ssid is NUL-terminated, rssi in 0..100, open=true when
// the network has no encryption.
struct WifiNetInfo {
    char ssid[33];
    int  rssi;
    bool open;
};
int platform_wifi_scan(WifiNetInfo* list, int max);   // returns count

// Real ICMP ping. bare: net_ping on the e1000 link; host: IcmpSendEcho.
// Returns true when a reply arrives within timeout_ms.
bool platform_ping(uint32_t ip, int timeout_ms);

// Real HTTP(S) GET. host: WinINet (DNS + TLS included); bare: returns false
// and the browser falls back to the in-house TCP stack (IP literals only).
// On success *out is a malloc'd buffer (caller frees), *out_size its length.
bool platform_http_get(const char* url, uint8_t** out, uint32_t* out_size);

// ===================== keycodes =====================
enum {
    KEY_NONE = 0,
    KEY_ENTER = 1,
    KEY_BACKSPACE = 2,
    KEY_ESC = 3,
    KEY_LEFT = 4, KEY_UP = 5, KEY_RIGHT = 6, KEY_DOWN = 7,
    KEY_TAB = 8,
    KEY_DEL = 9,
    KEY_HOME = 10, KEY_END = 11, KEY_PGUP = 12, KEY_PGDN = 13,
    KEY_SPACE = 14,
    KEY_F1 = 20, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_SHIFT = 30, KEY_CTRL = 31, KEY_ALT = 32, KEY_CAPS = 33,
};

} // namespace nefu
