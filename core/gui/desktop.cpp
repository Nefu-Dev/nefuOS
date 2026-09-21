// nefuOS desktop - LVGL 9.2.0 (MIT) is now the GUI engine for the desktop
// layer: wallpaper, icon grid, taskbar, live clock, start menu and context
// menus are all LVGL objects flushed through the nefuOS Surface. stb_truetype
// (MIT) stays available for TTF text via core/gui/ttfont.cpp.
// The native window manager keeps running on top of the LVGL desktop.
#include "desktop.h"
#include "../apps/apps.h"
#include "../sys/settings.h"
#include "../sys/power.h"
#include "../platform.h"
#include "lv_cjk_font.h"
#include "lvgl.h"

#if !defined(NEFU_BARE) && defined(_WIN32)
#include <cstdio>
#endif

LV_FONT_DECLARE(lv_font_montserrat_16);

namespace nefu {

// lock-screen state owned by nefuos.cpp
bool nefuos_is_locked();
int nefuos_lock_len();
const char* nefuos_lock_pwd();
uint32_t nefuos_lock_fail_ms();

static const int TASKBAR_H = 30;
static const int ICON_W = 72, ICON_H = 72;
static const int TILE = 52;

// aggregate-only (no ctor): bare kernel never runs C++ static ctors
struct DesktopIcon {
    int x, y;
    const char* label;
    int app;
    bool deleted;          // shortcut removed (app still installed)
};

static DesktopIcon s_icons[] = {
    {14, 14, "文件管理器", APP_FILEMGR, false},
    {14 + ICON_W + 8, 14, "终端", APP_TERMINAL, false},
    {14 + 2 * (ICON_W + 8), 14, "计算器", APP_CALC, false},
    {14, 14 + ICON_H + 10, "文本查看器", APP_TEXTVIEW, false},
    {14 + ICON_W + 8, 14 + ICON_H + 10, "数据库维基", APP_WIKI, false},
    {14, 14 + 2 * (ICON_H + 10), "设置", APP_SETTINGS, false},
    {14 + ICON_W + 8, 14 + 2 * (ICON_H + 10), "软件商店", APP_STORE, false},
    {14 + 2 * (ICON_W + 8), 14 + 2 * (ICON_H + 10), "图片查看器", APP_IMAGEVIEWER, false},
    {14, 14 + 3 * (ICON_H + 10), "音乐播放器", APP_MUSIC, false},
    {14 + ICON_W + 8, 14 + 3 * (ICON_H + 10), "系统监视器", APP_MONITOR, false},
    {14 + 2 * (ICON_W + 8), 14 + 3 * (ICON_H + 10), "浏览器", APP_BROWSER, false},
    {14, 14 + 4 * (ICON_H + 10), "网络", APP_NETCFG, false},
    {14 + ICON_W + 8, 14 + 4 * (ICON_H + 10), "应用启动器", APP_NEFUD, false},
    {14 + 2 * (ICON_W + 8), 14 + 4 * (ICON_H + 10), "日历", APP_CALENDAR, false},
    {14, 14 + 5 * (ICON_H + 10), "磁盘分析", APP_DISKUSAGE, false},
    {14 + ICON_W + 8, 14 + 5 * (ICON_H + 10), "密码生成器", APP_PASSGEN, false},
    {14 + 2 * (ICON_W + 8), 14 + 5 * (ICON_H + 10), "便签", APP_STICKY, false},
};
static const int s_icon_count = (int)(sizeof(s_icons) / sizeof(s_icons[0]));

static const char* icon_label(int app) {
    switch (app) {
    case APP_FILEMGR:    return T("文件管理器", "Files");
    case APP_TERMINAL:   return T("终端", "Terminal");
    case APP_CALC:       return T("计算器", "Calculator");
    case APP_TEXTVIEW:   return T("文本查看器", "Text View");
    case APP_WIKI:       return T("数据库维基", "Wiki");
    case APP_SYSINFO:    return T("系统信息", "System Info");
    case APP_SETTINGS:   return T("设置", "Settings");
    case APP_STORE:      return T("软件商店", "Store");
    case APP_IMAGEVIEWER:return T("图片查看器", "Images");
    case APP_MUSIC:      return T("音乐播放器", "Music");
    case APP_MONITOR:    return T("系统监视器", "Monitor");
    case APP_BROWSER:    return T("浏览器", "Browser");
    case APP_NETCFG:     return T("网络", "Network");
    case APP_NEFUD:      return T("应用启动器", "Launcher");
    default: return "?";
    }
}

// ---- LVGL desktop engine state ----
static lv_display_t* s_disp = 0;
static lv_indev_t* s_indev = 0;
static lv_color_t* s_fb_buf = 0;
static Surface* s_surf = 0;
static lv_obj_t* s_clock_label = 0;
static lv_obj_t* s_start_menu = 0;
static lv_obj_t* s_rmenu = 0;
static lv_obj_t* s_icon_objs[32];
static lv_obj_t* s_rmenu_items[2];
static int s_rmenu_icon = -1;
static bool s_start_open = false;
static bool s_rmenu_open = false;
static int s_mx = 10, s_my = 10;
static bool s_btn = false;
static bool s_initialized = false;
static uint8_t s_last_buttons = 0;
static uint32_t s_icon_color[32];
static bool s_dirty = false;
static uint32_t s_last_sig = 0;
static int s_menu_apps[32];
static int s_menu_count = 0;

// ---- LVGL keyboard input (keypad indev + shared group) ----
static uint32_t s_lv_keys[32];
static uint8_t s_lv_key_n = 0;
static lv_indev_t* s_kb_indev = 0;
static lv_group_t* s_kb_group = 0;

static void lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    (void)disp;
    if (!s_surf) {
        lv_display_flush_ready(disp);
        return;
    }
    Surface& s = *s_surf;
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    // px_map is ARGB8888 (4 bytes/px) for LV_COLOR_DEPTH=32; read as uint32
    const uint32_t* cmap = (const uint32_t*)px_map;
    for (int y = 0; y < h; y++) {
        int fy = area->y1 + y;
        if (fy < 0 || fy >= s.height) continue;
        for (int x = 0; x < w; x++) {
            int fx = area->x1 + x;
            if (fx < 0 || fx >= s.width) continue;
            uint32_t c = cmap[(size_t)y * (size_t)w + (size_t)x];  // 0xAARRGGBB little-endian
            s.setpx(fx, fy, c);
        }
    }
    lv_display_flush_ready(disp);
}


static uint32_t keycode_to_lv(int kc, char ascii) {
    switch (kc) {
        case KEY_UP: return LV_KEY_UP;
        case KEY_DOWN: return LV_KEY_DOWN;
        case KEY_LEFT: return LV_KEY_LEFT;
        case KEY_RIGHT: return LV_KEY_RIGHT;
        case KEY_ENTER: return LV_KEY_ENTER;
        case KEY_ESC: return LV_KEY_ESC;
        case KEY_BACKSPACE: return LV_KEY_BACKSPACE;
        case KEY_DEL: return LV_KEY_DEL;
        case KEY_TAB: return LV_KEY_NEXT;
        case KEY_HOME: return LV_KEY_HOME;
        case KEY_END: return LV_KEY_END;
        case KEY_PGUP: return LV_KEY_PREV;
        case KEY_PGDN: return LV_KEY_NEXT;
        default: break;
    }
    if (ascii == '\r' || ascii == '\n') return LV_KEY_ENTER;
    if (ascii == 27) return LV_KEY_ESC;
    if (ascii >= 32 && ascii < 127) return (uint32_t)ascii;
    return 0;
}

static void lvgl_key_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    if (s_lv_key_n) {
        data->key = s_lv_keys[0];
        data->state = LV_INDEV_STATE_PRESSED;
        for (uint8_t i = 0; i + 1 < s_lv_key_n; i++) s_lv_keys[i] = s_lv_keys[i + 1];
        s_lv_key_n--;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
void lvgl_key_push(int keycode, char ascii) {
    uint32_t k = keycode_to_lv(keycode, ascii);
    if (k && s_lv_key_n < 32) s_lv_keys[s_lv_key_n++] = k;
}

lv_group_t* lvgl_kb_group() {
    return s_kb_group;
}

static void lvgl_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    data->point.x = s_mx;
    data->point.y = s_my;
    data->state = s_btn ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    static int s_lvdbg = 0;
    if (s_lvdbg < 60 && (data->state == LV_INDEV_STATE_PRESSED || s_mx != 10 || s_my != 10)) {
        s_lvdbg++; klogf("lvgl in=%d,%d st=%d\n", s_mx, s_my, (int)data->state);
    }
}

void desktop_redraw() {
    Screen* scr = platform_screen();
    if (!scr) return;
    Surface fb;
    fb.addr = scr->addr;
    fb.width = scr->width;
    fb.height = scr->height;
    fb.pitch = scr->pitch;
    desktop_paint(fb);
    platform_present();
}

// ---- icon events ----
static void on_icon_click(lv_event_t* e) {
    // keyboard Enter on a focused icon must not launch apps: only a real
    // pointer click should. Otherwise pressing Enter inside an open window
    // accidentally launches the first desktop icon.
    lv_indev_t* indev = lv_event_get_indev(e);
    if (indev == s_kb_indev) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= 0 && i < s_icon_count && !s_icons[i].deleted) app_launch(s_icons[i].app);
}

static void on_icon_release(lv_event_t* e) {
    // snap dragged icon back to the grid, never overlapping
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= s_icon_count) return;
    DesktopIcon& ic = s_icons[i];
    lv_obj_t* obj = s_icon_objs[i];
    lv_obj_update_layout(obj);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    ic.x = a.x1;
    ic.y = a.y1;
    int col = (ic.x - 14 + (ICON_W + 8) / 2) / (ICON_W + 8);
    int row = (ic.y - 14 + (ICON_H + 10) / 2) / (ICON_H + 10);
    if (col < 0) col = 0;
    if (row < 0) row = 0;
    int best_c = col, best_r = row, best_d2 = 0x7FFFFFFF;
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            bool taken = false;
            for (int k = 0; k < s_icon_count; k++) {
                if (k == i || s_icons[k].deleted) continue;
                DesktopIcon& o = s_icons[k];
                int oc = (o.x - 14 + (ICON_W + 8) / 2) / (ICON_W + 8);
                int orw = (o.y - 14 + (ICON_H + 10) / 2) / (ICON_H + 10);
                if (oc == c && orw == r) { taken = true; break; }
            }
            if (!taken) {
                int d2 = (c - col) * (c - col) + (r - row) * (r - row);
                if (d2 < best_d2) { best_d2 = d2; best_c = c; best_r = r; }
            }
        }
    }
    ic.x = 14 + best_c * (ICON_W + 8);
    ic.y = 14 + best_r * (ICON_H + 10);
    lv_obj_set_pos(obj, ic.x, ic.y);
}

// ---- start menu ----
static void start_menu_toggle() {
    if (!s_start_menu) return;
    if (lv_obj_has_flag(s_start_menu, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(s_start_menu, LV_OBJ_FLAG_HIDDEN);
        s_start_open = true;
    } else {
        lv_obj_add_flag(s_start_menu, LV_OBJ_FLAG_HIDDEN);
        s_start_open = false;
    }
}

static void on_start_click(lv_event_t* e) {
    (void)e;
    start_menu_toggle();
}

static void on_menu_launch(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_start_menu) lv_obj_add_flag(s_start_menu, LV_OBJ_FLAG_HIDDEN);
    s_start_open = false;
    if (idx >= 0 && idx < s_menu_count) app_launch(s_menu_apps[idx]);
}

// ---- desktop context menu ----
static void show_rmenu(int icon_idx) {
    if (!s_rmenu) return;
    s_rmenu_icon = icon_idx;
    s_rmenu_open = true;
    lv_obj_remove_flag(s_rmenu, LV_OBJ_FLAG_HIDDEN);
}

static void hide_rmenu() {
    if (s_rmenu) lv_obj_add_flag(s_rmenu, LV_OBJ_FLAG_HIDDEN);
    s_rmenu_open = false;
    s_rmenu_icon = -1;
}

static void on_rmenu_open(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= 0 && i < s_icon_count && !s_icons[i].deleted) app_launch(s_icons[i].app);
    hide_rmenu();
}

static void on_rmenu_del(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= 0 && i < s_icon_count) {
        s_icons[i].deleted = true;      // shortcut only; app stays installed
        if (s_icon_objs[i]) lv_obj_add_flag(s_icon_objs[i], LV_OBJ_FLAG_HIDDEN);
    }
    hide_rmenu();
}

// ---- wallpaper ----
static void build_wallpaper(lv_obj_t* scr, int W, int H) {
    uint32_t top = 0x00345C86, bottom = 0x0088B7D8, base = color::CREAM;
    wallpaper_colors(g_settings.wallpaper, &top, &bottom, &base);
    lv_obj_set_style_bg_color(scr, lv_color_hex(base & 0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    // top gradient band (wallpaper)
    lv_obj_t* gband = lv_obj_create(scr);
    lv_obj_set_size(gband, W, H - TASKBAR_H);
    lv_obj_set_pos(gband, 0, 0);
    lv_obj_set_style_bg_color(gband, lv_color_hex(top & 0xFFFFFF), 0);
    lv_obj_set_style_bg_grad_color(gband, lv_color_hex(bottom & 0xFFFFFF), 0);
    lv_obj_set_style_bg_grad_dir(gband, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_stop(gband, 235, 0);
    lv_obj_set_style_bg_opa(gband, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(gband, 0, 0);
    lv_obj_set_style_radius(gband, 0, 0);
    // title watermark
    lv_obj_t* wm = lv_label_create(scr);
    lv_label_set_text(wm, "nefuOS 0.1  (LVGL GUI)");
    lv_obj_set_style_text_color(wm, lv_color_hex(0x60FFFFFF), 0);
    lv_obj_align(wm, LV_ALIGN_BOTTOM_RIGHT, -14, -TASKBAR_H - 16);
}

// ---- icon tile color helper ----
static uint32_t icon_color(int app) {
    switch (app) {
    case APP_FILEMGR:    return 0xE6B94A;
    case APP_TERMINAL:   return 0x3B4654;
    case APP_CALC:       return 0x3A6EA5;
    case APP_TEXTVIEW:   return 0xE8E6DF;
    case APP_WIKI:       return 0x2A7A6C;
    case APP_SETTINGS:   return 0x666C74;
    case APP_STORE:      return 0xE67E22;
    case APP_IMAGEVIEWER:return 0xF5F1E8;
    case APP_MUSIC:      return 0x14181E;
    case APP_MONITOR:    return 0x14181E;
    case APP_BROWSER:    return 0x5A9BD4;
    case APP_NETCFG:     return 0x3FA45A;
    case APP_NEFUD:      return 0xE67E22;
    case APP_CALENDAR:   return 0xE74C3C;
    case APP_DISKUSAGE:  return 0x9B59B6;
    case APP_PASSGEN:    return 0x1ABC9C;
    case APP_STICKY:     return 0xF1C40F;
    case APP_WEATHER:    return 0x3498DB;
    case APP_HELP:       return 0x9B59B6;
    case APP_DICTIONARY: return 0xE67E22;
    default:             return 0x8899AA;
    }
}

static lv_obj_t* make_icon(lv_obj_t* scr, int i, int x, int y) {
    DesktopIcon& ic = s_icons[i];
    s_icon_color[i] = icon_color(ic.app);
    lv_obj_t* card = lv_button_create(scr);
    lv_obj_set_size(card, ICON_W, ICON_H);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1E222C), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_30, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x40FFFFFF), 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    // LVGL v9 objects are draggable by default (press + move)
    // app tile
    lv_obj_t* tile = lv_obj_create(card);
    lv_obj_set_size(tile, TILE, TILE);
    lv_obj_align(tile, LV_ALIGN_TOP_MID, 0, 2);
    // tile must not capture the click: it has no CLICKED handler, and an
    // lv_obj is clickable+scrollable by default, so a press on the icon art
    // would never reach the parent card (no event bubbling). Make it inert so
    // LVGL's hit-test falls through to the card, which owns on_icon_click.
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(tile, lv_color_hex(s_icon_color[i]), 0);
    lv_obj_set_style_radius(tile, 6, 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(0x40000000), 0);
    lv_obj_set_style_bg_grad_color(tile, lv_color_hex(0x88FFFFFF), 0);
    lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_stop(tile, 60, 0);
    // label
    const char* lbl = icon_label(ic.app);
    lv_obj_t* lab = lv_label_create(card);
    lv_label_set_text(lab, lbl);
    lv_obj_set_style_text_color(lab, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lab, &lv_font_montserrat_14, 0);
    lv_obj_align(lab, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_add_event_cb(card, on_icon_click, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_add_event_cb(card, on_icon_release, LV_EVENT_RELEASED, (void*)(intptr_t)i);
    return card;
}

// start menu entries: built-in apps + settings + store + installed store apps
static int build_menu(int* list) {
    int n = 0;
    for (int i = 0; i < APP_BUILTIN_COUNT && n < 30; i++) list[n++] = i;
    if (n < 30) list[n++] = APP_SETTINGS;
    if (n < 30) list[n++] = APP_STORE;
    int ids[8];
    int cnt = app_installed_list(ids, 8);
    for (int i = 0; i < cnt && n < 30; i++) list[n++] = ids[i];
    return n;
}

// ---- taskbar + start menu ----
static void build_taskbar(lv_obj_t* scr, int W, int H) {
    lv_obj_t* bar = lv_obj_create(scr);
    lv_obj_set_size(bar, W, TASKBAR_H);
    lv_obj_set_pos(bar, 0, H - TASKBAR_H);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x262A31), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(0x4F5A66), 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_move_foreground(bar);

    // start button
    uint32_t accent = accent_color(g_settings.accent);
    lv_obj_t* sb = lv_button_create(bar);
    lv_obj_set_size(sb, 64, 24);
    lv_obj_set_pos(sb, 4, 3);
    lv_obj_set_style_bg_color(sb, lv_color_hex(accent & 0xFFFFFF), 0);
    lv_obj_set_style_radius(sb, 4, 0);
    lv_obj_add_event_cb(sb, on_start_click, LV_EVENT_CLICKED, 0);
    lv_obj_t* sl = lv_label_create(sb);
    lv_label_set_text(sl, "Start");
    lv_obj_set_style_text_color(sl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(sl);

    // clock
    s_clock_label = lv_label_create(bar);
    lv_label_set_text(s_clock_label, "--:--");
    lv_obj_set_style_text_color(s_clock_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_clock_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_clock_label, LV_ALIGN_RIGHT_MID, -10, 0);
}

static void build_start_menu(lv_obj_t* scr, int W, int H) {
    s_menu_count = build_menu(s_menu_apps);
    int rows = s_menu_count;
    int mw = 196;
    int mh = rows * 26 + 10 + 32;
    int mx = 4, my = H - TASKBAR_H - mh;
    if (my < 0) my = 0;
    s_start_menu = lv_obj_create(scr);
    lv_obj_set_size(s_start_menu, mw, mh);
    lv_obj_set_pos(s_start_menu, mx, my);
    lv_obj_set_style_bg_color(s_start_menu, lv_color_hex(0xF2F2F0), 0);
    lv_obj_set_style_bg_opa(s_start_menu, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_start_menu, 8, 0);
    lv_obj_set_style_border_width(s_start_menu, 1, 0);
    lv_obj_set_style_border_color(s_start_menu, lv_color_hex(0xA0A8B0), 0);
    lv_obj_set_style_pad_all(s_start_menu, 0, 0);
    lv_obj_add_flag(s_start_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_start_menu);

    int y = 4;
    for (int i = 0; i < rows; i++) {
        lv_obj_t* it = lv_button_create(s_start_menu);
        lv_obj_set_size(it, mw - 6, 24);
        lv_obj_set_pos(it, 3, y);
        lv_obj_set_style_bg_color(it, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_radius(it, 4, 0);
        lv_obj_add_event_cb(it, on_menu_launch, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_t* l = lv_label_create(it);
        lv_label_set_text(l, app_name(s_menu_apps[i]));
        lv_obj_set_style_text_color(l, lv_color_hex(0x22262A), 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 10, 0);
        y += 26;
    }
    lv_obj_t* sep = lv_obj_create(s_start_menu);
    lv_obj_set_size(sep, mw - 16, 1);
    lv_obj_set_pos(sep, 8, y + 4);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0xC0C4C8), 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    y += 10;
    lv_obj_t* po = lv_button_create(s_start_menu);
    lv_obj_set_size(po, mw - 6, 24);
    lv_obj_set_pos(po, 3, y);
    lv_obj_set_style_bg_color(po, lv_color_hex(0xF5DEDC), 0);
    lv_obj_set_style_radius(po, 4, 0);
    lv_obj_add_event_cb(po, on_menu_launch, LV_EVENT_CLICKED, (void*)(intptr_t)(-1));
    lv_obj_t* pl = lv_label_create(po);
    lv_label_set_text(pl, "Power Off");
    lv_obj_set_style_text_color(pl, lv_color_hex(0xC22A2A), 0);
    lv_obj_align(pl, LV_ALIGN_LEFT_MID, 10, 0);
}

static void build_rmenu(lv_obj_t* scr) {
    s_rmenu = lv_obj_create(scr);
    lv_obj_set_size(s_rmenu, 150, 50);
    lv_obj_set_pos(s_rmenu, 40, 40);
    lv_obj_set_style_bg_color(s_rmenu, lv_color_hex(0xF5F5F3), 0);
    lv_obj_set_style_bg_opa(s_rmenu, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_rmenu, 6, 0);
    lv_obj_set_style_border_width(s_rmenu, 1, 0);
    lv_obj_set_style_border_color(s_rmenu, lv_color_hex(0xA0A8B0), 0);
    lv_obj_set_style_pad_all(s_rmenu, 0, 0);
    lv_obj_add_flag(s_rmenu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_rmenu);
    const char* items[2] = { "Open", "Delete Shortcut" };
    for (int i = 0; i < 2; i++) {
        lv_obj_t* it = lv_button_create(s_rmenu);
        lv_obj_set_size(it, 144, 22);
        lv_obj_set_pos(it, 3, 3 + i * 22);
        lv_obj_set_style_bg_color(it, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_radius(it, 4, 0);
        lv_obj_t* l = lv_label_create(it);
        lv_label_set_text(l, items[i]);
        lv_obj_set_style_text_color(l, lv_color_hex(0x22262A), 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);
        s_rmenu_items[i] = it;
        lv_obj_add_event_cb(it, i == 0 ? on_rmenu_open : on_rmenu_del,
                            LV_EVENT_CLICKED, (void*)(intptr_t)s_rmenu_icon);
    }
}

static void on_clock_tick(lv_timer_t* t) {
    (void)t;
    if (!s_clock_label) return;
    uint32_t sec = platform_seconds_of_day();
    char buf[16];
    ksprintf(buf, sizeof(buf), "%02u:%02u", (sec / 3600) % 24, (sec / 60) % 60);
    lv_label_set_text(s_clock_label, buf);
}

// ---- public desktop API ----
static void desktop_lvgl_init(Surface& fb);

void desktop_init() {
    s_start_open = false;
    s_rmenu_open = false;
    s_initialized = false;
    s_dirty = true;
    s_last_sig = 0;
    Surface fb = screen_surface();
    desktop_lvgl_init(fb);
}

// Window overlays change the desktop pixels underneath (window moved / resized
// / opened / closed). Hash visible windows to detect exactly that; only then
// force a full LVGL redraw. Otherwise LVGL redraws only invalid regions, so
// an idle desktop costs almost no CPU (no busy-loop, no CPU saturation).
static uint32_t window_signature() {
    uint32_t sig = 0;
    List<Window*>& ws = g_wm->windows();
    for (int i = 0; i < ws.size(); i++) {
        Window* w = ws[i];
        if (!w->visible || w->closed) continue;
        sig = sig * 131u + (uint32_t)w->x * 7u + (uint32_t)w->y * 13u +
              (uint32_t)w->w * 17u + (uint32_t)w->h * 19u +
              (uint32_t)w->title.len() + (w->minimized ? 5u : 0u);
    }
    return sig;
}

// One-time LVGL initialisation. Called early from desktop_init() so that
// the host "--app N" mode (which never shows the lock screen before the
// app window is created) has a working LVGL allocator; desktop_paint()
// just skips this block once s_initialized is set.
static void desktop_lvgl_init(Surface& fb) {
    if (s_initialized) return;
    int W = fb.width, H = fb.height;
    lv_init();
    s_fb_buf = (lv_color_t*)kalloc((size_t)W * 24u * 4u);
    if (!s_fb_buf) { s_initialized = true; return; }
    s_disp = lv_display_create(W, H);
    lv_display_set_flush_cb(s_disp, lvgl_flush_cb);
    lv_display_set_buffers(s_disp, s_fb_buf, NULL,
                           (size_t)W * 24u * 4u, LV_DISPLAY_RENDER_MODE_PARTIAL);
    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, lvgl_read_cb);
    lv_indev_set_display(s_indev, s_disp);

    // keyboard input (keypad indev + shared group for text widgets)
    s_kb_indev = lv_indev_create();
    lv_indev_set_type(s_kb_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(s_kb_indev, lvgl_key_read_cb);
    lv_indev_set_display(s_kb_indev, s_disp);
    s_kb_group = lv_group_create();
    lv_indev_set_group(s_kb_indev, s_kb_group);
    lv_group_set_default(s_kb_group);

    lv_obj_t* scr = lv_screen_active();
    build_wallpaper(scr, W, H);
    for (int i = 0; i < s_icon_count; i++) {
        s_icon_objs[i] = make_icon(scr, i, s_icons[i].x, s_icons[i].y);
    }
    build_taskbar(scr, W, H);
    build_start_menu(scr, W, H);
    build_rmenu(scr);
    lv_timer_create(on_clock_tick, 1000, NULL);
    s_initialized = true;
    s_dirty = true;
}

void desktop_paint(Surface& fb) {
    desktop_lvgl_init(fb);
    if (!s_initialized) return;
    int W = fb.width, H = fb.height;
    // external overlay changed? then repaint the full desktop
    uint32_t sig = window_signature();
    if (sig != s_last_sig) { s_dirty = true; s_last_sig = sig; }
    if (s_dirty) {
        lv_obj_update_layout(lv_screen_active()); // layout first so coords exist
        lv_obj_invalidate(lv_screen_active());
        s_dirty = false;
    }
    s_surf = &fb;
    lv_tick_inc(16);
    lv_timer_handler();
    lv_refr_now(s_disp);
    s_surf = 0;
}

void desktop_paint_boot(Surface& fb) {
    fb.fill(0x0012141A);
    int W = fb.width, H = fb.height;
    gfx::text_scale(fb, W / 2 - 4 * 8 * 4, H / 2 - 90, "nefuOS", color::WHITE, 0x0012141A, 4);
    gfx::text(fb, W / 2 - 52, H / 2 - 18, "v0.1.0  C++/C  tiny OS", color::BLUE_LT, 0x0012141A);
    int bw = 300, bx = W / 2 - bw / 2, by = H / 2 + 16;
    gfx::rect(fb, bx, by, bw, 12, 0x00404A58);
    int p = (platform_tick_ms() / 40) % (bw - 8);
    gfx::fillrect(fb, bx + 3, by + 3, p, 6, color::BLUE_LT);
    gfx::text(fb, W / 2 - 30, by + 18, "Loading...", color::TEXT2, 0x0012141A);
}

// Lock screen: full-screen wallpaper + UEFI username + password gate. Shown
// between boot splash and the desktop (and on idle lock / suspend).
void desktop_paint_lock(Surface& fb) {
    uint32_t top, bottom, base;
    wallpaper_colors(g_settings.wallpaper_lock, &top, &bottom, &base);
    fb.fill(top);
    int W = fb.width, H = fb.height;
    // bottom shading band so the panel stands out
    for (int y = H - 200; y < H; y += 4) {
        int t = (y - (H - 200)) * 30 / 200;
        gfx::fillrect(fb, 0, y, W, 2,
                      ((t >> 3) << 16) | ((t >> 2) << 8) | (t >> 2) | 0xFF000000);
    }
    // big clock
    uint32_t sec = platform_seconds_of_day();
    char tbuf[24];
    ksprintf(tbuf, sizeof(tbuf), "%02u:%02u", (sec / 3600) % 24, (sec / 60) % 60);
    gfx::text_scale(fb, W / 2 - 9 * 8 * 3, H / 2 - 150, tbuf, color::WHITE, top, 3);
    gfx::text(fb, W / 2 - 40, H / 2 - 104, "nefuOS", color::WHITE, top);
    // login panel
    int px = W / 2 - 190, py = H / 2 - 40;
    gfx::fillrect(fb, px, py, 380, 132, 0xFF161C28);
    gfx::rect(fb, px, py, 380, 132, 0xFF3A4458);
    char line[96];
    ksprintf(line, sizeof(line), "User: %s",
             g_uefi.username[0] ? g_uefi.username : "user");
    gfx::text(fb, px + 16, py + 14, line, color::WHITE, 0xFF161C28);
    gfx::text(fb, px + 16, py + 46, "Password:", color::BLUE_LT, 0xFF161C28);
    int n = nefuos_lock_len();
    if (n > 40) n = 40;
    char stars[48];
    for (int i = 0; i < n; i++) stars[i] = '*';
    stars[n] = 0;
    gfx::text(fb, px + 120, py + 46, stars, color::WHITE, 0xFF161C28);
    gfx::text(fb, px + 16, py + 80, "Enter = unlock   ESC = reboot", color::TEXT2, 0xFF161C28);
    uint32_t now = platform_tick_ms();
    if (nefuos_lock_fail_ms() && now - nefuos_lock_fail_ms() < 1500)
        gfx::text(fb, px + 16, py + 104, "Wrong password - try again", color::RED, 0xFF161C28);
}

// First-boot setup wizard: full-screen account + browser-engine setup.
// Runs once after the boot splash when /var/lib/nefuos/firstboot is absent.
void desktop_paint_setup(Surface& fb) {
    uint32_t top, bottom, base;
    wallpaper_colors(g_settings.wallpaper_boot, &top, &bottom, &base);
    fb.fill(top);
    int W = fb.width, H = fb.height;
    // panel
    int pw = 560, ph = 396, px = W / 2 - pw / 2, py = H / 2 - ph / 2;
    gfx::fillrect(fb, px, py, pw, ph, 0xFF161C28);
    gfx::rect(fb, px, py, pw, ph, 0xFF3A4458);
    gfx::text_scale(fb, px + 24, py + 18, "nefuOS First Boot Setup", color::WHITE, 0xFF161C28, 2);
    gfx::text(fb, px + 24, py + 52, "Create your account and choose the browser engine.",
              color::TEXT2, 0xFF161C28);

    int fy = py + 88;                 // first field top
    int fh = 46, fw = pw - 48;
    const char* labels[4] = {
        "Username", "Password", "Confirm password", "Browser engine"
    };
    int cur = nefuos_setup_field();
    for (int f = 0; f < 4; f++) {
        int yy = fy + f * (fh + 10);
        gfx::text(fb, px + 24, yy, labels[f], color::BLUE_LT, 0xFF161C28);
        int vx = px + 24, vy = yy + 18, vw = fw - 48, vh = 22;
        uint32_t frame = (f == cur) ? color::BLUE : 0xFF3A4458;
        gfx::fillrect(fb, vx, vy, vw, vh, 0xFF0D1117);
        gfx::rect(fb, vx, vy, vw, vh, frame);
        char val[96];
        val[0] = 0;
        if (f == 0) {
            strncpy(val, nefuos_setup_user(), 31);
            val[31] = 0;
        } else if (f == 1 || f == 2) {
            int n = nefuos_setup_pwd_len(f);
            if (n > 40) n = 40;
            for (int i = 0; i < n; i++) val[i] = '*';
            val[n] = 0;
        } else {
            strcpy(val, nefuos_setup_engine() == 1 ? "No Script (HTML only)"
                                                   : "Mini JS (execute <script>)");
        }
        gfx::text(fb, vx + 6, vy + 5, val, color::WHITE, 0xFF0D1117);
        // blinking caret in the active text field
        if (f == cur && f < 3 && ((platform_tick_ms() / 500) & 1)) {
            int clen = (f == 0) ? (int)strlen(nefuos_setup_user()) : nefuos_setup_pwd_len(f);
            int cx = vx + 8 + clen * 8;
            if (cx < vx + vw - 4) gfx::fillrect(fb, cx, vy + 4, 2, vh - 8, color::BLUE_LT);
        }
        // engine selector hint
        if (f == 3) {
            gfx::text(fb, vx + 6 + (int)strlen(val) * 8 + 10, vy + 5,
                      "<  /  >", color::TEXT2, 0xFF0D1117);
        }
    }
    gfx::text(fb, px + 24, fy + 4 * (fh + 10) + 6,
              "Tab = next field   Enter = continue   <-  -> = choose   ESC = finish",
              color::TEXT2, 0xFF161C28);
    uint32_t fm = nefuos_setup_fail_ms();
    if (fm && platform_tick_ms() - fm < 2500) {
        gfx::text(fb, px + 24, fy + 4 * (fh + 10) + 26,
                  "Passwords do not match or are empty - try again",
                  color::RED, 0xFF161C28);
    }
}

bool desktop_handle_mouse(int x, int y, uint8_t buttons) {
    static int s_dhdbg = 0;
    if (s_dhdbg < 20) { s_dhdbg++; klogf("dh m=%d,%d b=%u\n", x, y, (unsigned)buttons); }
    int H = platform_screen()->height;
    bool pressed = (buttons & 1) != 0;
    bool r_pressed = (buttons & 2) != 0 && (s_last_buttons & 2) == 0;
    s_last_buttons = buttons;
    // windows take priority: never feed clicks under a window to LVGL.
    // BUT still update s_mx/s_my/s_btn so the LVGL indev sees the real
    // cursor position: otherwise the LVGL close (X) button in the window
    // header never receives a click and windows cannot be closed by it.
    if (g_wm->hit(x, y)) {
        s_mx = x;
        s_my = y;
        s_btn = pressed;
        return false;
    }
    // idle pointer moves only trigger LVGL-local hover repaints; the full
    // desktop is redrawn by desktop_paint when a window overlay changes.
    if (s_mx != x || s_my != y || s_btn != pressed) {
        s_mx = x;
        s_my = y;
        s_btn = pressed;
    }

    // right click on an icon -> context menu (LVGL panel shown on top)
    if (r_pressed) {
        for (int i = 0; i < s_icon_count; i++) {
            DesktopIcon& ic = s_icons[i];
            if (ic.deleted) continue;
            if (x >= ic.x && x < ic.x + ICON_W && y >= ic.y && y < ic.y + ICON_H) {
                s_rmenu_icon = i;
                if (s_rmenu) {
                    lv_obj_set_pos(s_rmenu, x < 140 ? x : x - 150, y < 60 ? y : y - 50);
                    lv_obj_remove_flag(s_rmenu, LV_OBJ_FLAG_HIDDEN);
                    s_rmenu_open = true;
                    // rebind item callbacks to this icon
                    if (s_rmenu_items[0]) {
                        lv_obj_remove_event_cb(s_rmenu_items[0], on_rmenu_open);
                        lv_obj_add_event_cb(s_rmenu_items[0], on_rmenu_open, LV_EVENT_CLICKED, (void*)(intptr_t)i);
                    }
                    if (s_rmenu_items[1]) {
                        lv_obj_remove_event_cb(s_rmenu_items[1], on_rmenu_del);
                        lv_obj_add_event_cb(s_rmenu_items[1], on_rmenu_del, LV_EVENT_CLICKED, (void*)(intptr_t)i);
                    }
                }
                return true;
            }
        }
        if (s_rmenu) { lv_obj_add_flag(s_rmenu, LV_OBJ_FLAG_HIDDEN); s_rmenu_open = false; }
        return true;
    }

    // click on blank desktop: dismiss menus, deselect
    if (pressed) {
        if (s_start_open && s_start_menu && !lv_obj_has_flag(s_start_menu, LV_OBJ_FLAG_HIDDEN)) {
            // let LVGL first check whether the click hit the menu;
            // if the click is outside the menu rect, close it.
            lv_area_t a;
            lv_obj_get_coords(s_start_menu, &a);
            if (x < a.x1 || x > a.x2 || y < a.y1 || y > a.y2) {
                lv_obj_add_flag(s_start_menu, LV_OBJ_FLAG_HIDDEN);
                s_start_open = false;
            }
        }
        if (s_rmenu_open && s_rmenu) {
            lv_area_t a;
            lv_obj_get_coords(s_rmenu, &a);
            if (x < a.x1 || x > a.x2 || y < a.y1 || y > a.y2) {
                lv_obj_add_flag(s_rmenu, LV_OBJ_FLAG_HIDDEN);
                s_rmenu_open = false;
            }
        }
    }
    return true; // desktop consumes clicks (LVGL widget events fire via timer)
}

bool desktop_handle_key(int keycode, char ascii) {
    lvgl_key_push(keycode, ascii);
    if (s_start_open && keycode == KEY_ESC) {
        if (s_start_menu) lv_obj_add_flag(s_start_menu, LV_OBJ_FLAG_HIDDEN);
        s_start_open = false;
        return true;
    }
    if (keycode == KEY_F1) { start_menu_toggle(); return true; }
    if (keycode == KEY_F2) { app_launch(APP_TERMINAL); return true; }
    if (keycode == KEY_F3) { app_launch(APP_FILEMGR); return true; }
    if (keycode == KEY_F4) { app_launch(APP_MUSIC); return true; }
    if (keycode == KEY_F5) { app_launch(APP_SETTINGS); return true; }
    if (keycode == KEY_F6) { app_launch(APP_BROWSER); return true; }
    return false;
}

} // namespace nefu
