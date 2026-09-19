// nefuOS LVGL Demo - integrates LVGL 9.2.0 (MIT) as a second GUI engine.
// A real LVGL display renders into a window: widgets (button, slider, arc,
// meter, checkbox, bar) update live. Input (mouse) is forwarded to the
// LVGL pointer device. The nefuOS window manager keeps running underneath,
// so the LVGL engine and the native GUI coexist.
//
// LVGL config: third_party/lvgl_conf/lv_conf.h (LV_COLOR_DEPTH=32,
// LV_USE_OS=LV_OS_NONE, LV_USE_FLOAT=0, built-in stdlib - no libc needed).
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/widgets.h"
#include "../gui/ttfont.h"

#include "lvgl.h"

// LVGL 9 font symbols are guarded by LV_FONT_MONTSERRAT_16 in lv_font.h;
// declare explicitly so the widget code always sees them.
LV_FONT_DECLARE(lv_font_montserrat_16);

namespace nefu {

struct LvglState {
    Window* win;
    Surface* surf;          // current target surface (window back)
    lv_display_t* disp;
    lv_indev_t* indev;
    lv_obj_t* counter_label;
    lv_obj_t* slider_value;
    int clicks;
    // input state
    int mx, my;
    bool btn;
    bool initialized;
    lv_color_t* fb_buf;     // partial render buffer
    int fb_rows;
};

static LvglState* g_lv = 0;

static void lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    (void)disp;
    if (!g_lv || !g_lv->surf) {
        lv_display_flush_ready(disp);
        return;
    }
    Surface& s = *g_lv->surf;
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
    if (!g_lv) return;
    data->point.x = g_lv->mx;
    data->point.y = g_lv->my;
    data->state = g_lv->btn ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// ---- widget event callbacks ----
static void on_counter_click(lv_event_t* e) {
    (void)e;
    if (!g_lv) return;
    g_lv->clicks++;
    char buf[48];
    lv_snprintf(buf, sizeof(buf), "clicks: %d", g_lv->clicks);
    lv_label_set_text(g_lv->counter_label, buf);
}

static void on_slider_change(lv_event_t* e) {
    if (!g_lv) return;
    lv_obj_t* sl = (lv_obj_t*)lv_event_get_target(e);
    int32_t v = lv_slider_get_value(sl);
    char buf[48];
    lv_snprintf(buf, sizeof(buf), "slider: %" LV_PRId32, v);
    lv_label_set_text(g_lv->slider_value, buf);
}

static void lvgl_build_ui() {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1E1E2E), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // title
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "LVGL 9.2.0 running on nefuOS");
    lv_obj_set_style_text_color(title, lv_color_hex(0xCDD6F4), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    // counter button
    lv_obj_t* btn = lv_button_create(scr);
    lv_obj_set_size(btn, 150, 44);
    lv_obj_align(btn, LV_ALIGN_CENTER, -120, -40);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x89B4FA), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_add_event_cb(btn, on_counter_click, LV_EVENT_CLICKED, 0);
    lv_obj_t* btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Click me");
    lv_obj_center(btn_label);

    // counter readout
    g_lv->counter_label = lv_label_create(scr);
    lv_label_set_text(g_lv->counter_label, "clicks: 0");
    lv_obj_set_style_text_color(g_lv->counter_label, lv_color_hex(0xA6E3A1), 0);
    lv_obj_align(g_lv->counter_label, LV_ALIGN_CENTER, 60, -40);

    // slider
    lv_obj_t* sl = lv_slider_create(scr);
    lv_obj_set_size(sl, 200, 16);
    lv_obj_align(sl, LV_ALIGN_CENTER, 0, 30);
    lv_slider_set_range(sl, 0, 100);
    lv_slider_set_value(sl, 40, LV_ANIM_OFF);
    lv_obj_add_event_cb(sl, on_slider_change, LV_EVENT_VALUE_CHANGED, 0);

    g_lv->slider_value = lv_label_create(scr);
    lv_label_set_text(g_lv->slider_value, "slider: 40");
    lv_obj_set_style_text_color(g_lv->slider_value, lv_color_hex(0xF9E2AF), 0);
    lv_obj_align(g_lv->slider_value, LV_ALIGN_CENTER, 0, 58);

    // arc (dial) and progress bar show the drawing pipeline
    lv_obj_t* arc = lv_arc_create(scr);
    lv_obj_set_size(arc, 110, 110);
    lv_obj_align(arc, LV_ALIGN_BOTTOM_LEFT, 20, -16);
    lv_arc_set_value(arc, 65);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x94E2D5), LV_PART_INDICATOR);

    // checkbox
    lv_obj_t* cb = lv_checkbox_create(scr);
    lv_checkbox_set_text(cb, "LVGL engine active");
    lv_obj_align(cb, LV_ALIGN_BOTTOM_RIGHT, -16, -60);
    lv_obj_set_style_text_color(cb, lv_color_hex(0xBAC2DE), 0);

    // progress bar
    lv_obj_t* bar = lv_bar_create(scr);
    lv_obj_set_size(bar, 180, 14);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_RIGHT, -16, -24);
    lv_bar_set_value(bar, 65, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x74C7EC), LV_PART_INDICATOR);
}

// ---- nefuOS window glue ----
static void lvgl_paint(Window* w) {
    LvglState* st = (LvglState*)w->userdata;
    if (!st->initialized) {
        st->surf = &w->back;
        st->initialized = true;
    }
    // the native WM redraws every frame; keep the surface persistent
    // so LVGL partial flushes accumulate correctly.
    st->surf = &w->back;
    lv_tick_inc(16);
    lv_timer_handler();
}

static void lvgl_mouse(Window* w, int mx, int my, uint8_t buttons) {
    (void)w;
    LvglState* st = (LvglState*)w->userdata;
    st->mx = mx;
    st->my = my;
    st->btn = (buttons != 0);
}

static void lvgl_close(Window* w) {
    LvglState* st = (LvglState*)w->userdata;
    if (st) {
        if (st->disp) {
            lv_display_delete(st->disp);
            st->disp = 0;
        }
        if (st->fb_buf) { kfree(st->fb_buf); st->fb_buf = 0; }
        delete st;
        w->userdata = 0;
    }
    g_lv = 0;
}

void lvgl_demo_launch() {
    lv_init();
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("LVGL Demo", x, y, 640, 420);
    if (!w) return;
    LvglState* st = new LvglState();
    st->win = w;
    st->surf = &w->back;
    st->clicks = 0;
    st->mx = 10; st->my = 10; st->btn = false;
    st->initialized = false;
    st->counter_label = 0;
    st->slider_value = 0;

    // LVGL partial render buffer: 640 x 32 rows x 4 bytes
    st->fb_rows = 32;
    st->fb_buf = (lv_color_t*)kalloc(640u * 32u * 4u);
    if (!st->fb_buf) { delete st; return; }

    st->disp = lv_display_create(640, 420);
    lv_display_set_flush_cb(st->disp, lvgl_flush_cb);
    lv_display_set_buffers(st->disp, st->fb_buf, NULL,
                           640u * 32u * 4u, LV_DISPLAY_RENDER_MODE_PARTIAL);

    st->indev = lv_indev_create();
    lv_indev_set_type(st->indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(st->indev, lvgl_read_cb);
    lv_indev_set_display(st->indev, st->disp);

    g_lv = st;
    lvgl_build_ui();

    w->userdata = st;
    w->on_paint = lvgl_paint;
    w->on_mouse = lvgl_mouse;
    w->on_close = lvgl_close;
}

} // namespace nefu
