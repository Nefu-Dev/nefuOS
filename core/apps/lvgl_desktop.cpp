// nefuOS LVGL Desktop - LVGL 9.2.0 (MIT) drives a full desktop environment:
// wallpaper, app-icon grid, taskbar with live uptime clock and a start menu.
// Clicking an icon closes the LVGL desktop and launches the native app, so
// the LVGL engine and the nefuOS window manager stay compatible.
//
// LVGL config: third_party/lvgl_conf/lv_conf.h (LV_COLOR_DEPTH=32,
// LV_USE_OS=LV_OS_NONE, LV_USE_FLOAT=0, built-in stdlib - no libc needed).
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/widgets.h"
#include "../gui/wm.h"
#include "../platform.h"
#include "lvgl.h"

// LVGL 9 font symbols are guarded by LV_FONT_MONTSERRAT_16 in lv_font.h;
// declare explicitly so the widget code always sees them.
LV_FONT_DECLARE(lv_font_montserrat_16);

namespace nefu {

struct LvDesktop {
    Window* win;
    Surface* surf;
    lv_display_t* disp;
    lv_indev_t* indev;
    lv_obj_t* clock_label;
    lv_obj_t* start_menu;
    lv_obj_t* start_btn;
    lv_timer_t* clock_timer;
    int pending_launch;      // >=0: launch this app after closing desktop
    int mx, my;
    bool btn;
    bool initialized;
    lv_color_t* fb_buf;
    int fb_rows;
};

static LvDesktop* g_ld = 0;

struct LvIcon { const char* name; AppId id; uint32_t color; };

static const LvIcon s_icons[] = {
    {"Files",      APP_FILEMGR,     0x89B4FA},
    {"Terminal",   APP_TERMINAL,    0x94E2D5},
    {"Calc",       APP_CALC,        0xF9E2AF},
    {"TextView",   APP_TEXTVIEW,    0xA6E3A1},
    {"Wiki",       APP_WIKI,        0xCBA6F7},
    {"Settings",   APP_SETTINGS,    0xF38BA8},
    {"Store",      APP_STORE,       0x74C7EC},
    {"Images",     APP_IMAGEVIEWER, 0xFAB387},
    {"Music",      APP_MUSIC,       0xF5C2E7},
    {"Monitor",    APP_MONITOR,     0x74C7EC},
    {"Browser",    APP_BROWSER,     0x89DCEB},
    {"Network",    APP_NETCFG,      0xB4BEFE},
    {"Launcher",   APP_NEFUD,       0xCDD6F4},
    {"Fonts",      APP_FONTVIEW,    0xEBA0AC},
    {"LVGL Demo",  APP_LVGLDEMO,    0x94E2D5},
};
static const int s_icon_count = (int)(sizeof(s_icons) / sizeof(s_icons[0]));

// ---- LVGL display glue ----
static void lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    (void)disp;
    if (!g_ld || !g_ld->surf) {
        lv_display_flush_ready(disp);
        return;
    }
    Surface& s = *g_ld->surf;
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

static void lvgl_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    if (!g_ld) return;
    data->point.x = g_ld->mx;
    data->point.y = g_ld->my;
    data->state = g_ld->btn ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// ---- wallpaper: layered bands + subtle grid, no images needed ----
static void lv_wallpaper(lv_obj_t* scr, int w, int h) {
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0F0F17), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    // three translucent accent bands
    lv_obj_t* band = lv_obj_create(scr);
    lv_obj_set_size(band, w, 140);
    lv_obj_align(band, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(band, lv_color_hex(0x1E2030), 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_80, 0);
    lv_obj_set_style_border_width(band, 0, 0);
    lv_obj_set_style_radius(band, 0, 0);
    band = lv_obj_create(scr);
    lv_obj_set_size(band, w, 90);
    lv_obj_align(band, LV_ALIGN_TOP_LEFT, 0, h - 260);
    lv_obj_set_style_bg_color(band, lv_color_hex(0x181A28), 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_60, 0);
    lv_obj_set_style_border_width(band, 0, 0);
    lv_obj_set_style_radius(band, 0, 0);
    // big title
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "nefuOS");
    lv_obj_set_style_text_color(title, lv_color_hex(0xCDD6F4), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_letter_space(title, 6, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 24, 18);
    lv_obj_t* sub = lv_label_create(scr);
    lv_label_set_text(sub, "LVGL 9.2.0 desktop  /  C++ + stb_truetype");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x6C7086), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 26, 40);
}

// ---- app icon buttons ----
static void on_icon_click(lv_event_t* e) {
    AppId id = (AppId)(intptr_t)lv_event_get_user_data(e);
    if (g_ld) g_ld->pending_launch = (int)id;
}

static lv_obj_t* lv_make_icon(lv_obj_t* scr, int x, int y, const LvIcon& ic) {
    lv_obj_t* card = lv_button_create(scr);
    lv_obj_set_size(card, 96, 84);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x181A28), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_60, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2A2D3E), 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    // color chip (acts as the glyph)
    lv_obj_t* chip = lv_obj_create(card);
    lv_obj_set_size(chip, 34, 34);
    lv_obj_align(chip, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_bg_color(chip, lv_color_hex(ic.color), 0);
    lv_obj_set_style_radius(chip, 8, 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    // label
    lv_obj_t* lab = lv_label_create(card);
    lv_label_set_text(lab, ic.name);
    lv_obj_set_style_text_color(lab, lv_color_hex(0xC3C9E0), 0);
    lv_obj_set_style_text_font(lab, &lv_font_montserrat_16, 0);
    lv_obj_align(lab, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_add_event_cb(card, on_icon_click, LV_EVENT_CLICKED, (void*)(intptr_t)ic.id);
    return card;
}

// ---- taskbar ----
static void on_start_click(lv_event_t* e) {
    (void)e;
    if (!g_ld || !g_ld->start_menu) return;
    bool hidden = lv_obj_has_flag(g_ld->start_menu, LV_OBJ_FLAG_HIDDEN);
    if (hidden) lv_obj_remove_flag(g_ld->start_menu, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_add_flag(g_ld->start_menu, LV_OBJ_FLAG_HIDDEN);
}

static void on_quick_click(lv_event_t* e) {
    AppId id = (AppId)(intptr_t)lv_event_get_user_data(e);
    if (g_ld) g_ld->pending_launch = (int)id;
}

static void on_menu_item(lv_event_t* e) {
    AppId id = (AppId)(intptr_t)lv_event_get_user_data(e);
    if (!g_ld) return;
    if (g_ld->start_menu) lv_obj_add_flag(g_ld->start_menu, LV_OBJ_FLAG_HIDDEN);
    g_ld->pending_launch = (int)id;
}

static void lv_build_taskbar(lv_obj_t* scr, int w, int h) {
    lv_obj_t* bar = lv_obj_create(scr);
    lv_obj_set_size(bar, w, 44);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x14151F), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(0x2A2D3E), 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);

    // start button
    g_ld->start_btn = lv_button_create(bar);
    lv_obj_set_size(g_ld->start_btn, 92, 36);
    lv_obj_align(g_ld->start_btn, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_bg_color(g_ld->start_btn, lv_color_hex(0x89B4FA), 0);
    lv_obj_set_style_radius(g_ld->start_btn, 8, 0);
    lv_obj_add_event_cb(g_ld->start_btn, on_start_click, LV_EVENT_CLICKED, 0);
    lv_obj_t* st_lab = lv_label_create(g_ld->start_btn);
    lv_label_set_text(st_lab, "nefuOS");
    lv_obj_set_style_text_color(st_lab, lv_color_hex(0x11111B), 0);
    lv_obj_center(st_lab);

    // quick launch buttons
    const char* qn[] = { "Files", "Term", "Sett", "Demo" };
    const AppId qid[] = { APP_FILEMGR, APP_TERMINAL, APP_SETTINGS, APP_LVGLDEMO };
    for (int i = 0; i < 4; i++) {
        lv_obj_t* b = lv_button_create(bar);
        lv_obj_set_size(b, 58, 30);
        lv_obj_align(b, LV_ALIGN_LEFT_MID, 108 + i * 66, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x1E2030), 0);
        lv_obj_set_style_radius(b, 6, 0);
        lv_obj_add_event_cb(b, on_quick_click, LV_EVENT_CLICKED, (void*)(intptr_t)qid[i]);
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, qn[i]);
        lv_obj_set_style_text_color(l, lv_color_hex(0xC3C9E0), 0);
        lv_obj_center(l);
    }

    // clock
    g_ld->clock_label = lv_label_create(bar);
    lv_label_set_text(g_ld->clock_label, "0:00:00");
    lv_obj_set_style_text_color(g_ld->clock_label, lv_color_hex(0xCDD6F4), 0);
    lv_obj_set_style_text_font(g_ld->clock_label, &lv_font_montserrat_16, 0);
    lv_obj_align(g_ld->clock_label, LV_ALIGN_RIGHT_MID, -14, 0);

    // start menu panel (hidden)
    g_ld->start_menu = lv_obj_create(scr);
    lv_obj_set_size(g_ld->start_menu, 230, 216);
    lv_obj_align(g_ld->start_menu, LV_ALIGN_BOTTOM_LEFT, 4, -48);
    lv_obj_set_style_bg_color(g_ld->start_menu, lv_color_hex(0x1E2030), 0);
    lv_obj_set_style_bg_opa(g_ld->start_menu, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_ld->start_menu, 12, 0);
    lv_obj_set_style_border_width(g_ld->start_menu, 1, 0);
    lv_obj_set_style_border_color(g_ld->start_menu, lv_color_hex(0x3A3D52), 0);
    lv_obj_add_flag(g_ld->start_menu, LV_OBJ_FLAG_HIDDEN);

    const char* mn[] = { "Settings", "Terminal", "File Manager", "LVGL Demo" };
    const AppId mid[] = { APP_SETTINGS, APP_TERMINAL, APP_FILEMGR, APP_LVGLDEMO };
    for (int i = 0; i < 4; i++) {
        lv_obj_t* it = lv_button_create(g_ld->start_menu);
        lv_obj_set_size(it, 210, 42);
        lv_obj_align(it, LV_ALIGN_TOP_LEFT, 10, 8 + i * 50);
        lv_obj_set_style_bg_color(it, lv_color_hex(0x262A3A), 0);
        lv_obj_set_style_radius(it, 8, 0);
        lv_obj_add_event_cb(it, on_menu_item, LV_EVENT_CLICKED, (void*)(intptr_t)mid[i]);
        lv_obj_t* l = lv_label_create(it);
        lv_label_set_text(l, mn[i]);
        lv_obj_set_style_text_color(l, lv_color_hex(0xE4E8F5), 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 12, 0);
    }
}

static void on_clock_tick(lv_timer_t* t) {
    (void)t;
    if (!g_ld || !g_ld->clock_label) return;
    uint32_t s = nefuos_uptime_ms() / 1000;
    char buf[24];
    lv_snprintf(buf, sizeof(buf), "%u:%02u:%02u", s / 3600, (s % 3600) / 60, s % 60);
    lv_label_set_text(g_ld->clock_label, buf);
}

// ---- nefuOS window glue ----
static void lv_paint(Window* w) {
    LvDesktop* st = (LvDesktop*)w->userdata;
    if (!st) return;
    if (!st->initialized) {
        st->surf = &w->back;
        st->initialized = true;
    }
    // launch pending target after closing this desktop
    if (st->pending_launch >= 0) {
        int id = st->pending_launch;
        st->pending_launch = -1;
        Window* ww = st->win;
        g_wm->close_window(ww);   // triggers on_close -> frees LVGL state
        app_launch(id);
        return;
    }
    st->surf = &w->back;
    lv_tick_inc(16);
    lv_timer_handler();
}

static void lv_mouse(Window* w, int mx, int my, uint8_t buttons) {
    (void)w;
    LvDesktop* st = (LvDesktop*)w->userdata;
    if (!st) return;
    st->mx = mx;
    st->my = my;
    st->btn = (buttons != 0);
}

static void lv_close(Window* w) {
    LvDesktop* st = (LvDesktop*)w->userdata;
    if (st) {
        if (st->clock_timer) { lv_timer_delete(st->clock_timer); st->clock_timer = 0; }
        if (st->disp) { lv_display_delete(st->disp); st->disp = 0; }
        if (st->fb_buf) { kfree(st->fb_buf); st->fb_buf = 0; }
        delete st;
        w->userdata = 0;
    }
    g_ld = 0;
}

void lvgl_desktop_launch() {
    lv_init();
    const int DW = 1000, DH = 740;
    Window* w = g_wm->create_window("LVGL Desktop", 0, 0, DW, DH);
    if (!w) return;
    LvDesktop* st = new LvDesktop();
    st->win = w;
    st->surf = &w->back;
    st->pending_launch = -1;
    st->mx = 10; st->my = 10; st->btn = false;
    st->initialized = false;
    st->clock_label = 0;
    st->start_menu = 0;
    st->start_btn = 0;
    st->clock_timer = 0;

    st->fb_rows = 32;
    st->fb_buf = (lv_color_t*)kalloc((size_t)DW * 32u * 4u);
    if (!st->fb_buf) { delete st; return; }

    st->disp = lv_display_create(DW, DH);
    lv_display_set_flush_cb(st->disp, lvgl_flush_cb);
    lv_display_set_buffers(st->disp, st->fb_buf, NULL,
                           (size_t)DW * 32u * 4u, LV_DISPLAY_RENDER_MODE_PARTIAL);

    st->indev = lv_indev_create();
    lv_indev_set_type(st->indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(st->indev, lvgl_read_cb);
    lv_indev_set_display(st->indev, st->disp);

    g_ld = st;

    lv_obj_t* scr = lv_screen_active();
    lv_wallpaper(scr, DW, DH);
    // icon grid: 5 columns
    const int GW = 104, GH = 96;
    for (int i = 0; i < s_icon_count; i++) {
        int col = i % 5, row = i / 5;
        lv_make_icon(scr, 20 + col * GW, 66 + row * GH, s_icons[i]);
    }
    lv_build_taskbar(scr, DW, DH);

    st->clock_timer = lv_timer_create(on_clock_tick, 1000, NULL);

    w->userdata = st;
    w->on_paint = lv_paint;
    w->on_mouse = lv_mouse;
    w->on_close = lv_close;
}

} // namespace nefu
