// nefuOS settings - LVGL GUI (accent / wallpaper / clock / taskbar / language / power / hw info)
#include "apps.h"
#include "../gui/lvgl_win.h"
#include "../sys/settings.h"
#include "../sys/power.h"
#include "../platform.h"

namespace nefu {

static const char* ACCENT_NAMES[4] = { "Blue", "Green", "Purple", "Orange" };
static const char* WALL_NAMES[4] = { "Classic", "Sunset", "Dark", "Custom" };

struct SettingsLvState {
    LvglWin* lw;
    lv_obj_t* btns[28];
};

static void settings_lv_click(lv_event_t* e) {
    SettingsLvState* st = (SettingsLvState*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    int id = (int)(intptr_t)lv_obj_get_user_data(btn);
    if (id >= 0 && id <= 3) {
        g_settings.accent = id;
    } else if (id >= 4 && id <= 7) {
        g_settings.wallpaper = id - 4;
    } else if (id >= 8 && id <= 11) {
        g_settings.wallpaper_lock = id - 8;
    } else if (id >= 12 && id <= 15) {
        g_settings.wallpaper_boot = id - 12;
    } else if (id == 16) {
        g_settings.show_clock = !g_settings.show_clock;
    } else if (id == 17) {
        g_settings.show_taskbar = !g_settings.show_taskbar;
    } else if (id == 18) {
        g_settings.lang = 0;
    } else if (id == 19) {
        g_settings.lang = 1;
    } else if (id == 20) {
        g_settings.boot_splash = !g_settings.boot_splash;
    } else if (id == 24) {
        g_settings.lock_on_suspend = !g_settings.lock_on_suspend;
    } else if (id == 25) {
        g_settings.idle_lock_sec = g_settings.idle_lock_sec ? 0 : 30;
    } else if (id == 26) {
        g_settings.browser_engine = g_settings.browser_engine ? 0 : 1;
    } else if (id == 21) {
        os_shutdown();
    } else if (id == 22) {
        os_reboot();
    } else if (id == 23) {
        os_suspend();
    }
    settings_save();
    if (id == 16) {
        lv_obj_t* lbl = lv_obj_get_child(btn, 0);
        if (lbl) lv_label_set_text(lbl, g_settings.show_clock ? "[x] Show clock" : "[ ] Show clock");
    }
    if (id == 17) {
        lv_obj_t* lbl = lv_obj_get_child(btn, 0);
        if (lbl) lv_label_set_text(lbl, g_settings.show_taskbar ? "[x] Show taskbar" : "[ ] Show taskbar");
    }
    if (id == 20) {
        lv_obj_t* lbl = lv_obj_get_child(btn, 0);
        if (lbl) lv_label_set_text(lbl, g_settings.boot_splash ? "[x] Show boot splash" : "[ ] Show boot splash");
    }
    if (id == 24) {
        lv_obj_t* lbl = lv_obj_get_child(btn, 0);
        if (lbl) lv_label_set_text(lbl, g_settings.lock_on_suspend ? "[x] Lock on suspend" : "[ ] Lock on suspend");
    }
    if (id == 25) {
        lv_obj_t* lbl = lv_obj_get_child(btn, 0);
        if (lbl) lv_label_set_text(lbl, g_settings.idle_lock_sec ? "[x] Auto-lock after 30s idle" : "[ ] Auto-lock idle (off)");
    }
    if (id == 26) {
        lv_obj_t* lbl = lv_obj_get_child(btn, 0);
        if (lbl) lv_label_set_text(lbl, g_settings.browser_engine ? "Browser engine: NoJS" : "Browser engine: MiniJS");
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
    sec_label(lw->content, "Personalize nefuOS", 12, 6);
    sec_label(lw->content, "Accent color:", 12, 28);
    for (int i = 0; i < 4; i++)
        st->btns[i] = make_btn(st, lw->content, ACCENT_NAMES[i], 12 + i * 108, 46, 100, 26, i, 0x3D4B66);

    sec_label(lw->content, "Desktop wallpaper:", 12, 78);
    for (int i = 0; i < 4; i++)
        st->btns[4 + i] = make_btn(st, lw->content, WALL_NAMES[i], 12 + i * 108, 96, 100, 26, 4 + i, 0x3D4B66);

    sec_label(lw->content, "Lock screen wallpaper:", 12, 126);
    for (int i = 0; i < 4; i++)
        st->btns[8 + i] = make_btn(st, lw->content, WALL_NAMES[i], 12 + i * 108, 144, 100, 26, 8 + i, 0x3D4B66);

    sec_label(lw->content, "Boot splash wallpaper:", 12, 174);
    for (int i = 0; i < 4; i++)
        st->btns[12 + i] = make_btn(st, lw->content, WALL_NAMES[i], 12 + i * 108, 192, 100, 26, 12 + i, 0x3D4B66);

    // Display & language section
    st->btns[16] = make_btn(st, lw->content, g_settings.show_clock ? "[x] Show clock" : "[ ] Show clock",
                           12, 224, 160, 26, 16, 0x55678A);
    st->btns[17] = make_btn(st, lw->content, g_settings.show_taskbar ? "[x] Show taskbar" : "[ ] Show taskbar",
                           184, 224, 180, 26, 17, 0x55678A);
    st->btns[20] = make_btn(st, lw->content, g_settings.boot_splash ? "[x] Show boot splash" : "[ ] Show boot splash",
                           12, 254, 180, 26, 20, 0x55678A);
    st->btns[24] = make_btn(st, lw->content, g_settings.lock_on_suspend ? "[x] Lock on suspend" : "[ ] Lock on suspend",
                           200, 254, 180, 26, 24, 0x55678A);
    st->btns[25] = make_btn(st, lw->content, g_settings.idle_lock_sec ? "[x] Auto-lock after 30s idle" : "[ ] Auto-lock idle (off)",
                           12, 284, 260, 26, 25, 0x55678A);
    st->btns[26] = make_btn(st, lw->content, g_settings.browser_engine ? "Browser engine: NoJS" : "Browser engine: MiniJS",
                           12, 314, 220, 26, 26, 0x55678A);

    sec_label(lw->content, "Language:", 12, 348);
    st->btns[18] = make_btn(st, lw->content, "English", 12, 366, 84, 26, 18, 0x3D4B66);
    st->btns[19] = make_btn(st, lw->content, "Chinese", 104, 366, 84, 26, 19, 0x3D4B66);

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
    sec_label(lw->content, "Power:", 12, 452);
    st->btns[21] = make_btn(st, lw->content, "Shutdown", 12, 470, 100, 30, 21, 0x993333);
    st->btns[22] = make_btn(st, lw->content, "Reboot", 118, 470, 100, 30, 22, 0x336699);
    st->btns[23] = make_btn(st, lw->content, "Suspend", 224, 470, 100, 30, 23, 0x555577);

    sec_label(lw->content, "All settings are saved to /etc/settings.conf and persist across reboots", 12, 508);
}
} // namespace nefu