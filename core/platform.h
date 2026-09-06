// nefuOS 平台抽象层
// 核心（core）只依赖本文件声明的接口；后端（win32 / bare）分别实现。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {

// image decode: host backend uses the OS decoder (jpg/png/gif/bmp);
// bare backend returns false and the caller falls back to built-in bmp/ppm
struct Surface;
bool platform_decode_image(const uint8_t* data, uint32_t size, struct Surface& out);

// ===================== 内存 =====================
void* kalloc(size_t sz);
void  kfree(void* p);

// ===================== 屏幕 =====================
// 软件像素缓冲，32bpp，内存字节序 BGRA（小端 uint32 = 0x00RRGGBB）
struct Screen {
    uint8_t* addr;
    int width;
    int height;
    int pitch;      // 每行字节数（>= width*4）
};
Screen* platform_screen();
// 宿主后端每帧后把缓冲呈现到真实屏幕（裸机为空实现）
void platform_present();

// ===================== 时间 =====================
uint32_t platform_tick_ms();        // 自启动毫秒数
uint32_t platform_seconds_of_day();
uint32_t nefuos_uptime_ms();          // nefuOS 自启动起的毫秒数 // 当天秒数（裸机用开机时长代替）

// ===================== 调试输出 =====================
void platform_dbg(const char* s);

// ===================== 核心回调（后端调用） =====================
void nefuos_handle_key(int keycode, char ascii, bool down);
void nefuos_handle_mouse(int x, int y, uint8_t buttons);
void nefuos_handle_scroll(int delta);
void nefuos_tick();     // 周期心跳（时钟/动画）
void nefuos_frame();    // 每帧重绘
void nefuos_init();     // 系统初始化
void nefuos_shutdown(); // 系统退出清理（保存 VFS 等）

// 内存统计（信息展示用）
void platform_mem_stats(uint32_t* used, uint32_t* total);
const char* platform_name();

// ===================== 持久化（宿主实现，裸机空实现） =====================
bool platform_fs_load(uint8_t** out, uint32_t* out_size);
void platform_fs_save(const uint8_t* data, uint32_t size);

// ===================== 关机/退出 =====================
void platform_poweroff();

// ===================== 网络（真实适配器信息） =====================
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

// host: real Wi-Fi scan via WlanGetNetworkBssList; bare: returns 0 (e1000
// is wired). ssid is NUL-terminated, rssi in 0..100, open=true when the
// network has no encryption.
struct WifiNetInfo {
    char ssid[33];
    int  rssi;
    bool open;
};
int platform_wifi_scan(WifiNetInfo* list, int max);   // returns count

// Real ICMP ping. bare: net_ping on the e1000 link; host: IcmpSendEcho.
// Returns true when a reply arrives within timeout_ms.
bool platform_ping(uint32_t ip, int timeout_ms);

// ===================== 键码 =====================
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
