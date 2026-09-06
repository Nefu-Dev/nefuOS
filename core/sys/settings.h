// nefuOS 系统设置：主题色 / 壁纸 / 时钟显示，持久化到 /etc/settings.conf
#pragma once
#include "../vfs/vfs.h"

namespace nefu {

struct SysSettings {
    int accent;        // 0=蓝 1=绿 2=紫 3=橙
    int wallpaper;     // 0=默认 1=落日 2=深色
    bool show_clock;   // 任务栏时钟
    bool show_taskbar; // 任务栏显隐
    int lang;          // 0=English, 1=Chinese
    SysSettings() : accent(0), wallpaper(0), show_clock(true), show_taskbar(true), lang(0) {}
};

extern SysSettings g_settings;

void settings_load();   // 从 /etc/settings.conf 读取
void settings_save();   // 写回 /etc/settings.conf

// 主题强调色（标题栏 / 开始按钮）
uint32_t accent_color(int idx);

// 壁纸顶部色（供桌面渐变使用）
void wallpaper_colors(int idx, uint32_t* top, uint32_t* bottom, uint32_t* base);

// simple UI language helper: 0 = English, 1 = Chinese
inline const char* T(const char* zh, const char* en) {
    return g_settings.lang ? zh : en;
}

} // namespace nefu
