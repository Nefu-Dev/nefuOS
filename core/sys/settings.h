// nefuOS ：primary color / wallpaper / ， /etc/settings.conf
#pragma once
#include "../vfs/vfs.h"

namespace nefu {

struct SysSettings {
    int accent;        // 0=blue 1=green 2=purple 3=orange
    int wallpaper;     // 0= 1=sunset 2=dark
    bool show_clock;
    bool show_taskbar;
    int lang;          // 0=English, 1=Chinese
    SysSettings() : accent(0), wallpaper(0), show_clock(true), show_taskbar(true), lang(0) {}
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
