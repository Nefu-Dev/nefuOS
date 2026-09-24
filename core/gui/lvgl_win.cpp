// nefuOS LVGL application window container implementation
#include "lvgl_win.h"
#include "lv_cjk_font.h"
#include <cstring>
#include <stddef.h>
#include "../klib/klib.h"
#include "../platform.h"

// LVGL heap: allocated from the nefuOS kernel heap at runtime (see lv_conf.h
// LV_MEM_POOL_ALLOC) so the 1 MiB pool is not embedded in kernel.bin.
extern "C" void* nefu_lvgl_pool_alloc(size_t size) {
    return ::nefu::kalloc(size);
}

namespace nefu {

// Close button callback: deletes the lv_win, marks the wrapper closed.
static void win_close_cb(lv_event_t* e) {
    LvglWin* w = (LvglWin*)lv_event_get_user_data(e);
    if (w && w->win) {
        w->open = false;
        lv_obj_delete(w->win);
        w->win = 0;
        w->content = 0;
    }
}

LvglWin* lvgl_win_create(const char* title, int x, int y, int w, int h) {
    // Clamp window geometry to the screen work area, same policy as
    // WM::create_window. Without this every LVGL window (Settings 480x700,
    // Notepad 560px wide at a cascade offset, BIOS, ...) could extend past the
    // 800x600 screen edge and become unreachable. Fullscreen-style windows
    // (800x600 at 0,0, e.g. boot splash / lock screen / BSOD) are exempt.
    {
        const int SCREEN_W = 800;
        const int SCREEN_H = 600;
        const int TASKBAR_H = 30;
        const bool fullscreen_like = (x == 0 && y == 0 && w >= SCREEN_W && h >= SCREEN_H);
        if (!fullscreen_like) {
            const int MAX_W = SCREEN_W - 8;
            const int MAX_H = SCREEN_H - TASKBAR_H - 8;
            if (w > MAX_W) w = MAX_W;
            if (h > MAX_H) h = MAX_H;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (x + w > SCREEN_W) x = SCREEN_W - w;
            if (y + h > SCREEN_H - TASKBAR_H) y = SCREEN_H - TASKBAR_H - h;
        }
    }

    LvglWin* r = new LvglWin();
    if (!r) return 0;
    memset(r, 0, sizeof(*r));
    r->win = lv_win_create(lv_screen_active());
    if (!r->win) {
        delete r;
        return 0;
    }
    lv_obj_set_size(r->win, w, h);
    lv_obj_set_pos(r->win, x, y);
    lv_obj_set_style_radius(r->win, 6, 0);
    lv_obj_set_style_border_color(r->win, lv_color_hex(0x3A4458), 0);
    lv_obj_set_style_border_width(r->win, 1, 0);
    lv_obj_set_style_shadow_width(r->win, 12, 0);
    lv_obj_set_style_shadow_opa(r->win, LV_OPA_50, 0);

    // Title bar with window title text
    lv_obj_t* hdr = lv_win_get_header(r->win);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x232B3D), 0);
    lv_obj_set_style_pad_left(hdr, 10, 0);
    lv_obj_t* title_lbl = lv_label_create(hdr);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_14, 0);

    // Close button on the right side of the header
    lv_obj_t* close_btn = lv_win_add_button(r->win, LV_SYMBOL_CLOSE, 34);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x3D4B66), 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xC23B3B), LV_STATE_PRESSED);
    lv_obj_add_event_cb(close_btn, win_close_cb, LV_EVENT_CLICKED, r);

    // Maximize/fullscreen button
    lv_obj_t* max_btn = lv_win_add_button(r->win, LV_SYMBOL_NEW_LINE, 34);
    lv_obj_set_style_bg_color(max_btn, lv_color_hex(0x3D4B66), 0);
    lv_obj_set_style_bg_color(max_btn, lv_color_hex(0x4A9EFF), LV_STATE_PRESSED);
    // TODO: add maximize callback

    // Minimize button
    lv_obj_t* min_btn = lv_win_add_button(r->win, LV_SYMBOL_DOWN, 34);
    lv_obj_set_style_bg_color(min_btn, lv_color_hex(0x3D4B66), 0);
    lv_obj_set_style_bg_color(min_btn, lv_color_hex(0x9ECE6E), LV_STATE_PRESSED);
    // TODO: add minimize callback

    // Content area: light panel, no padding inside
    r->content = lv_win_get_content(r->win);
    lv_obj_set_style_bg_color(r->content, lv_color_hex(0xEDF0F6), 0);
    lv_obj_set_style_bg_opa(r->content, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(r->content, 0, 0);

    r->open = true;
    return r;
}

void lvgl_win_set_title(LvglWin* w, const char* t) {
    if (!w || !w->win) return;
    lv_obj_t* hdr = lv_win_get_header(w->win);
    // first child label is the title we created
    lv_obj_t* lbl = lv_obj_get_child(hdr, 0);
    if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
        lv_label_set_text(lbl, t);
    }
}

void lvgl_win_close(LvglWin* w) {
    if (!w) return;
    if (w->win) {
        w->open = false;
        lv_obj_delete(w->win);
        w->win = 0;
        w->content = 0;
    }
}

} // namespace nefu
