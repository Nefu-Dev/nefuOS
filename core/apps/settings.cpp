// nefuOS settings - LVGL GUI (accent / wallpaper / clock / taskbar / language / power / hw info)
#include "apps.h"
#include "../gui/lvgl_win.h"
#include "../sys/settings.h"
#include "../sys/power.h"
#include "../platform.h"

namespace nefu {

static const char* ACCENT_NAMES[4] = { "Blue", "Green", "Purple", "Orange" };
static const char* WALL_NAMES[6] = {
    T("蔚蓝天空", "Blue Sky"), T("落日余晖", "Sunset"), T("暗夜", "Dark"),
    T("绿意森林", "Green Forest"), T("紫色幻境", "Purple"), T("橙韵暖阳", "Orange")
};

struct SettingsLvState {
    LvglWin* lw;
    lv_obj_t* btns[33];
};

static void settings_lv_click(lv_event_t* e) {
    SettingsLvState* st = (SettingsLvState*)lv_event_get_user_data(e);
    (void)st;
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

void settings_launch() {
    int x, y;
    cascade_pos(&x, &y);
    // 480x545 fits the 800x600 work area (600 - 30 taskbar - margins);
    // lvgl_win_create additionally clamps size/position as a safety net.
    LvglWin* lw = lvgl_win_create("Settings", x, y, 480, 545);
    if (!lw) return;
    SettingsLvState* st = new SettingsLvState();
    st->lw = lw;
    lw->userdata = st;

    // Personalization section
    sec_label(lw->content, T("个性化 nefuOS", "Personalize nefuOS"), 12, 6);
    sec_label(lw->content, T("强调色:", "Accent color:"), 12, 28);
    for (int i = 0; i < 4; i++)
        st->btns[i] = make_btn(st, lw->content, ACCENT_NAMES[i], 12 + i * 108, 46, 100, 26, i, 0x3D4B66);

    sec_label(lw->content, T("桌面壁纸:", "Desktop wallpaper:"), 12, 78);
    for (int i = 0; i < 6; i++)
        st->btns[4 + i] = make_btn(st, lw->content, WALL_NAMES[i], 12 + (i % 3) * 108, 96 + (i / 3) * 30, 100, 26, 4 + i, 0x3D4B66);

    sec_label(lw->content, T("锁屏壁纸:", "Lock screen wallpaper:"), 12, 142);
    for (int i = 0; i < 6; i++)
        st->btns[10 + i] = make_btn(st, lw->content, WALL_NAMES[i], 12 + (i % 3) * 108, 160 + (i / 3) * 30, 100, 26, 10 + i, 0x3D4B66);

    sec_label(lw->content, T("开机启动壁纸:", "Boot splash wallpaper:"), 12, 206);
    for (int i = 0; i < 6; i++)
        st->btns[16 + i] = make_btn(st, lw->content, WALL_NAMES[i], 12 + (i % 3) * 108, 224 + (i / 3) * 30, 100, 26, 16 + i, 0x3D4B66);

    // Display & language section
    st->btns[22] = make_btn(st, lw->content, g_settings.show_clock ? T("[x] 显示时钟", "[x] Show clock") : T("[ ] 显示时钟", "[ ] Show clock"),
                           12, 292, 160, 26, 22, 0x55678A);
    st->btns[23] = make_btn(st, lw->content, g_settings.show_taskbar ? T("[x] 显示任务栏", "[x] Show taskbar") : T("[ ] 显示任务栏", "[ ] Show taskbar"),
                           184, 292, 180, 26, 23, 0x55678A);
    st->btns[24] = make_btn(st, lw->content, g_settings.boot_splash ? T("[x] 开机动画", "[x] Show boot splash") : T("[ ] 开机动画", "[ ] Show boot splash"),
                           12, 322, 180, 26, 24, 0x55678A);
    st->btns[25] = make_btn(st, lw->content, g_settings.lock_on_suspend ? T("[x] 挂起后锁定", "[x] Lock on suspend") : T("[ ] 挂起后锁定", "[ ] Lock on suspend"),
                           200, 322, 180, 26, 25, 0x55678A);
    st->btns[26] = make_btn(st, lw->content, g_settings.idle_lock_sec ? T("[x] 空闲 30 秒自动锁定", "[x] Auto-lock after 30s idle") : T("[ ] 空闲自动锁定", "[ ] Auto-lock idle (off)"),
                           12, 352, 260, 26, 26, 0x55678A);
    st->btns[27] = make_btn(st, lw->content, g_settings.browser_engine ? T("浏览器引擎: NoJS", "Browser engine: NoJS") : T("浏览器引擎: MiniJS", "Browser engine: MiniJS"),
                           12, 382, 220, 26, 27, 0x55678A);

    sec_label(lw->content, T("语言:", "Language:"), 12, 416);
    st->btns[28] = make_btn(st, lw->content, "English", 12, 434, 84, 26, 28, 0x3D4B66);
    st->btns[29] = make_btn(st, lw->content, "中文", 104, 434, 84, 26, 29, 0x3D4B66);

    // Hardware info section
    HwInfo hw;
    platform_hw_info(&hw);
    char hw_txt[256];
    ksprintf(hw_txt, sizeof(hw_txt), "CPU: %s @ %dMHz", hw.cpu_model, hw.cpu_mhz);
    sec_label(lw->content, hw_txt, 12, 404);
    ksprintf(hw_txt, sizeof(hw_txt), "Memory: %d MB total", (int)hw.mem_total_mb);
    sec_label(lw->content, hw_txt, 12, 412);
    ksprintf(hw_txt, sizeof(hw_txt), "BIOS: %s %s", hw.bios_vendor, hw.bios_version);
    sec_label(lw->content, hw_txt, 12, 426);

    // Power operations section
    sec_label(lw->content, T("电源:", "Power:"), 12, 488);
    st->btns[30] = make_btn(st, lw->content, T("关机", "Shutdown"), 12, 506, 100, 30, 30, 0x993333);
    st->btns[31] = make_btn(st, lw->content, T("重启", "Reboot"), 118, 506, 100, 30, 31, 0x336699);
    st->btns[32] = make_btn(st, lw->content, T("挂起", "Suspend"), 224, 506, 100, 30, 32, 0x555577);

    sec_label(lw->content, "All settings are saved to /etc/settings.conf and persist across reboots", 12, 508);
}
} // namespace nefu