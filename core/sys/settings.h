// nefuOS primary color / wallpaper / configuration, persisted to /etc/settings.conf
#pragma once
#include "../vfs/vfs.h"

namespace nefu {

struct SysSettings {
    int accent;        // 0=blue 1=green 2=purple 3=orange
    int wallpaper;     // Desktop wallpaper: 0=blue, 1=sunset, 2=dark, 3=custom
    int wallpaper_lock;// Lock screen / suspend wallpaper
    int wallpaper_boot;// Boot splash screen wallpaper
    bool show_clock;
    bool show_taskbar;
    int lang;          // 0=English, 1=Chinese
    bool lock_on_suspend; // Require password on resume from suspend
    bool boot_splash;     // Show boot splash screen during startup
    int  idle_lock_sec;   // Idle timeout in seconds to auto-lock, 0 = disabled
    char autostart_apps[256]; // Semicolon-separated list of apps to launch on boot
    SysSettings() : accent(0), wallpaper(0), wallpaper_lock(0), wallpaper_boot(0),
        show_clock(true), show_taskbar(true), lang(0),
        lock_on_suspend(true), boot_splash(true), idle_lock_sec(0), autostart_apps("") {}
};

extern SysSettings g_settings;

void settings_load();   // from /etc/settings.conf read
void settings_save();   // /etc/settings.conf

// （ / start button）
uint32_t accent_color(int idx);

// （）
void wallpaper_colors(int idx, uint32_t* top, uint32_t* bottom, uint32_t* base);

// simple UI language helper: 0 = English, 1 = Chinese
inline const char* T(const char* zh, const char* en) {
    return g_settings.lang ? zh : en;
}

} // namespace nefu
