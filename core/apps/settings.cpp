/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS settings - LVGL GUI (accent / wallpaper / clock / taskbar / language / power / hw info)
#include "apps.h"
#include "../gui/lvgl_win.h"
#include "../gui/desktop.h"
#include "../sys/settings.h"
#include "../sys/power.h"
#include "../platform.h"

namespace nefu {

// Looked up at build time, not at static-init time: T() reads g_settings.lang,
// and a file-scope array would freeze the labels to whatever language was
// active before any config file had been read (English), so switching to
// Chinese would leave these buttons English.
static const char* accent_name(int i) {
    static const char* zh[4] = { "蓝", "绿", "紫", "橙" };
    static const char* en[4] = { "Blue", "Green", "Purple", "Orange" };
    return T(zh[i & 3], en[i & 3]);
}
static const char* wall_name(int i) {
    static const char* zh[6] = { "蔚蓝天空", "落日余晖", "暗夜", "绿意森林", "紫色幻境", "橙韵暖阳" };
    static const char* en[6] = { "Blue Sky", "Sunset", "Dark", "Green Forest", "Purple", "Orange" };
    return T(zh[i % 6], en[i % 6]);
}

struct SettingsLvState {
    LvglWin* lw;
    lv_obj_t* btns[33];
};

static void settings_build_ui(SettingsLvState* st);       // forward decl
static void settings_rebuild_async(void* p);              // forward decl

static void settings_lv_click(lv_event_t* e) {
    SettingsLvState* st = (SettingsLvState*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    int id = (int)(intptr_t)lv_obj_get_user_data(btn);
    if (id >= 0 && id <= 3) {
        g_settings.accent = id;
    } else if (id >= 4 && id <= 9) {
        g_settings.wallpaper = id - 4;
    } else if (id >= 10 && id <= 15) {
        g_settings.wallpaper_lock = id - 10;
    } else if (id >= 16 && id <= 21) {
        g_settings.wallpaper_boot = id - 16;
    } else if (id == 22) {
        g_settings.show_clock = !g_settings.show_clock;
    } else if (id == 23) {
        g_settings.show_taskbar = !g_settings.show_taskbar;
    } else if (id == 24) {
        g_settings.boot_splash = !g_settings.boot_splash;
    } else if (id == 25) {
        g_settings.lock_on_suspend = !g_settings.lock_on_suspend;
    } else if (id == 26) {
        g_settings.idle_lock_sec = g_settings.idle_lock_sec ? 0 : 30;
    } else if (id == 27) {
        g_settings.browser_engine = g_settings.browser_engine ? 0 : 1;
    } else if (id == 28) {
        g_settings.lang = 0;
    } else if (id == 29) {
        g_settings.lang = 1;
    } else if (id == 30) {
        os_shutdown();
    } else if (id == 31) {
        os_reboot();
    } else if (id == 32) {
        os_suspend();
    }
    settings_save();
    // Language buttons: switch the whole UI over right now (page rebuild is
    // deferred to the next timer tick, see settings_rebuild_async).
    if (id == 28 || id == 29) {
        lv_async_call(settings_rebuild_async, st);
    }
    if (id == 22) {
        lv_obj_t* lbl = lv_obj_get_child(btn, 0);
        if (lbl) lv_label_set_text(lbl, g_settings.show_clock ? T("[x] 显示时钟", "[x] Show clock") : T("[ ] 显示时钟", "[ ] Show clock"));
    }
    if (id == 23) {
        lv_obj_t* lbl = lv_obj_get_child(btn, 0);
        if (lbl) lv_label_set_text(lbl, g_settings.show_taskbar ? T("[x] 显示任务栏", "[x] Show taskbar") : T("[ ] 显示任务栏", "[ ] Show taskbar"));
    }
    if (id == 24) {
        lv_obj_t* lbl = lv_obj_get_child(btn, 0);
        if (lbl) lv_label_set_text(lbl, g_settings.boot_splash ? T("[x] 开机动画", "[x] Show boot splash") : T("[ ] 开机动画", "[ ] Show boot splash"));
    }
    if (id == 25) {
        lv_obj_t* lbl = lv_obj_get_child(btn, 0);
        if (lbl) lv_label_set_text(lbl, g_settings.lock_on_suspend ? T("[x] 挂起后锁定", "[x] Lock on suspend") : T("[ ] 挂起后锁定", "[ ] Lock on suspend"));
    }
    if (id == 26) {
        lv_obj_t* lbl = lv_obj_get_child(btn, 0);
        if (lbl) lv_label_set_text(lbl, g_settings.idle_lock_sec ? T("[x] 空闲 30 秒自动锁定", "[x] Auto-lock after 30s idle") : T("[ ] 空闲自动锁定", "[ ] Auto-lock idle (off)"));
    }
    if (id == 27) {
        lv_obj_t* lbl = lv_obj_get_child(btn, 0);
        if (lbl) lv_label_set_text(lbl, g_settings.browser_engine ? T("浏览器引擎: NoJS", "Browser engine: NoJS") : T("浏览器引擎: MiniJS", "Browser engine: MiniJS"));
    }
}

static lv_obj_t* make_btn(SettingsLvState* st, lv_obj_t* parent, const char* label,
                          int x, int y, int w, int h, int id, uint32_t bg) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2B3347), LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, settings_lv_click, LV_EVENT_CLICKED, st);
    lv_obj_set_user_data(b, (void*)(intptr_t)id);
    lv_obj_t* lbl = lv_label_create(b);
    lv_label_set_text(lbl, label);
    lv_obj_center(lbl);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    return b;
}

static lv_obj_t* sec_label(lv_obj_t* parent, const char* text, int x, int y) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_color(l, lv_color_hex(0x15181E), 0);
    return l;
}

// Build (or rebuild) the whole settings page inside the window's content area.
// Called once when the window opens and again right after a language switch,
// so the page always speaks the language that is active now.
static void settings_build_ui(SettingsLvState* st) {
    if (!st || !st->lw || !st->lw->content) return;
    LvglWin* lw = st->lw;

    // Personalization section
    sec_label(lw->content, T("个性化 nefuOS", "Personalize nefuOS"), 12, 6);
    sec_label(lw->content, T("强调色:", "Accent color:"), 12, 28);
    for (int i = 0; i < 4; i++)
        st->btns[i] = make_btn(st, lw->content, accent_name(i), 12 + i * 108, 46, 100, 26, i, 0x3D4B66);

    sec_label(lw->content, T("桌面壁纸:", "Desktop wallpaper:"), 12, 78);
    for (int i = 0; i < 6; i++)
        st->btns[4 + i] = make_btn(st, lw->content, wall_name(i), 12 + (i % 3) * 108, 96 + (i / 3) * 30, 100, 26, 4 + i, 0x3D4B66);

    sec_label(lw->content, T("锁屏壁纸:", "Lock screen wallpaper:"), 12, 160);
    for (int i = 0; i < 6; i++)
        st->btns[10 + i] = make_btn(st, lw->content, wall_name(i), 12 + (i % 3) * 108, 178 + (i / 3) * 30, 100, 26, 10 + i, 0x3D4B66);

    sec_label(lw->content, T("开机启动壁纸:", "Boot splash wallpaper:"), 12, 238);
    for (int i = 0; i < 6; i++)
        st->btns[16 + i] = make_btn(st, lw->content, wall_name(i), 12 + (i % 3) * 108, 256 + (i / 3) * 30, 100, 26, 16 + i, 0x3D4B66);

    // Display & language section
    st->btns[22] = make_btn(st, lw->content, g_settings.show_clock ? T("[x] 显示时钟", "[x] Show clock") : T("[ ] 显示时钟", "[ ] Show clock"),
                           12, 316, 160, 26, 22, 0x55678A);
    st->btns[23] = make_btn(st, lw->content, g_settings.show_taskbar ? T("[x] 显示任务栏", "[x] Show taskbar") : T("[ ] 显示任务栏", "[ ] Show taskbar"),
                           184, 316, 180, 26, 23, 0x55678A);
    st->btns[24] = make_btn(st, lw->content, g_settings.boot_splash ? T("[x] 开机动画", "[x] Show boot splash") : T("[ ] 开机动画", "[ ] Show boot splash"),
                           12, 346, 180, 26, 24, 0x55678A);
    st->btns[25] = make_btn(st, lw->content, g_settings.lock_on_suspend ? T("[x] 挂起后锁定", "[x] Lock on suspend") : T("[ ] 挂起后锁定", "[ ] Lock on suspend"),
                           200, 346, 180, 26, 25, 0x55678A);
    st->btns[26] = make_btn(st, lw->content, g_settings.idle_lock_sec ? T("[x] 空闲 30 秒自动锁定", "[x] Auto-lock after 30s idle") : T("[ ] 空闲自动锁定", "[ ] Auto-lock idle (off)"),
                           12, 376, 260, 26, 26, 0x55678A);
    st->btns[27] = make_btn(st, lw->content, g_settings.browser_engine ? T("浏览器引擎: NoJS", "Browser engine: NoJS") : T("浏览器引擎: MiniJS", "Browser engine: MiniJS"),
                           12, 406, 220, 26, 27, 0x55678A);

    sec_label(lw->content, T("语言:", "Language:"), 12, 436);
    st->btns[28] = make_btn(st, lw->content, "English", 12, 454, 84, 26, 28, 0x3D4B66);
    st->btns[29] = make_btn(st, lw->content, "中文", 104, 454, 84, 26, 29, 0x3D4B66);

    // Hardware info section (kept below the language buttons: the old y values
    // collided with them and printed on top of the toggle row)
    HwInfo hw;
    platform_hw_info(&hw);
    char hw_txt[256];
    ksprintf(hw_txt, sizeof(hw_txt), T("CPU: %s @ %dMHz", "CPU: %s @ %dMHz"), hw.cpu_model, hw.cpu_mhz);
    sec_label(lw->content, T("硬件信息:", "Hardware:"), 12, 496);
    sec_label(lw->content, hw_txt, 12, 514);
    ksprintf(hw_txt, sizeof(hw_txt), T("内存: 共 %d MB", "Memory: %d MB total"), (int)hw.mem_total_mb);
    sec_label(lw->content, hw_txt, 12, 528);
    ksprintf(hw_txt, sizeof(hw_txt), T("BIOS: %s %s", "BIOS: %s %s"), hw.bios_vendor, hw.bios_version);
    sec_label(lw->content, hw_txt, 12, 542);

    // Power operations section
    sec_label(lw->content, T("电源:", "Power:"), 12, 566);
    st->btns[30] = make_btn(st, lw->content, T("关机", "Shutdown"), 12, 586, 100, 30, 30, 0x993333);
    st->btns[31] = make_btn(st, lw->content, T("重启", "Reboot"), 118, 586, 100, 30, 31, 0x336699);
    st->btns[32] = make_btn(st, lw->content, T("挂起", "Suspend"), 224, 586, 100, 30, 32, 0x555577);

    sec_label(lw->content, T("所有设置保存在 /etc/settings.conf,重启后依然生效",
                             "All settings are saved to /etc/settings.conf and persist across reboots"),
              12, 624);
}

// Deferred UI rebuild after a language change. Deferred because the click
// handler runs inside an LVGL event: deleting the button that fired the event
// right there would free the object under LVGL's feet. lv_async_call runs it
// from the next lv_timer_handler tick instead.
static void settings_rebuild_async(void* p) {
    SettingsLvState* st = (SettingsLvState*)p;
    if (!st || !st->lw || !st->lw->content || !st->lw->open) return;
    klogf("settings: rebuild begin lang=%d", g_settings.lang);
    lv_obj_t* c = st->lw->content;
    for (int i = 0; i < 33; i++) st->btns[i] = 0;
    lv_obj_clean(c);                 // drop every label/button of the old language
    klogf("settings: cleaned");
    settings_build_ui(st);
    klogf("settings: ui rebuilt");
    lvgl_win_set_title(st->lw, T("设置", "Settings"));   // title bar + taskbar button
    klogf("settings: title set");
    desktop_relabel_lang();          // icons + Start button + taskbar follow
    klogf("settings: desktop relabelled");
}

void settings_launch() {
    int x, y;
    cascade_pos(&x, &y);
    // The window is clamped to the live screen work area by lvgl_win_create,
    // so a 480x545 page still fits on any resolution.
    LvglWin* lw = lvgl_win_create(T("设置", "Settings"), x, y, 480, 545);
    if (!lw) return;
    SettingsLvState* st = new SettingsLvState();
    st->lw = lw;
    lw->userdata = st;
    settings_build_ui(st);
}

} // namespace nefu
