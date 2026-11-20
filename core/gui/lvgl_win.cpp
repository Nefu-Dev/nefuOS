/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
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

// ---- live-window registry (the taskbar reads it) ----
// Fixed table: no heap churn on bare metal and no static constructors.
static const int LVW_MAX = 24;
static LvglWin* s_wins[LVW_MAX];
static int      s_win_n = 0;
static uint32_t s_win_sig = 0;

static void reg_add(LvglWin* w) {
    if (s_win_n >= LVW_MAX) return;      // keep the oldest; the desktop still works
    s_wins[s_win_n++] = w;
    s_win_sig++;
}

static void reg_del(LvglWin* w) {
    for (int i = 0; i < s_win_n; i++) {
        if (s_wins[i] == w) {
            for (int j = i; j < s_win_n - 1; j++) s_wins[j] = s_wins[j + 1];
            s_win_n--;
            w->open = false;
            w->minimized = false;
            s_win_sig++;
            return;
        }
    }
}

int lvgl_win_count() { return s_win_n; }
LvglWin* lvgl_win_at(int i) { return (i >= 0 && i < s_win_n) ? s_wins[i] : 0; }
uint32_t lvgl_win_signature() { return s_win_sig; }

bool lvgl_win_minimize(LvglWin* w) {
    if (!w || !w->win || !w->open || w->system_window) return false;
    if (w->minimized) return true;
    w->minimized = true;
    lv_obj_add_flag(w->win, LV_OBJ_FLAG_HIDDEN);
    s_win_sig++;                        // the taskbar needs a new button
    return true;
}

bool lvgl_win_restore(LvglWin* w) {
    if (!w || !w->win || !w->open) return false;
    if (!w->minimized) {
        lv_obj_move_foreground(w->win);
        return true;
    }
    w->minimized = false;
    lv_obj_remove_flag(w->win, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(w->win);
    s_win_sig++;
    return true;
}

bool lvgl_win_zoom_toggle(LvglWin* w) {
    if (!w || !w->win || !w->open || w->system_window) return false;
    int sw = 800, sh = 600;
    Screen* scr = platform_screen();
    if (scr) { sw = scr->width; sh = scr->height; }
    const int work_h = sh - 30;            // leave the taskbar reachable
    if (!w->zoomed) {
        lv_obj_update_layout(w->win);
        lv_area_t a;
        lv_obj_get_coords(w->win, &a);
        w->x = a.x1; w->y = a.y1;
        w->w = lv_obj_get_width(w->win);
        w->h = lv_obj_get_height(w->win);
        lv_obj_set_size(w->win, sw, work_h);
        lv_obj_set_pos(w->win, 0, 0);
        w->zoomed = true;
    } else {
        lv_obj_set_size(w->win, w->w, w->h);
        lv_obj_set_pos(w->win, w->x, w->y);
        w->zoomed = false;
    }
    lv_obj_move_foreground(w->win);
    return true;
}

// Close button callback: deletes the lv_win, marks the wrapper closed.
static void win_close_cb(lv_event_t* e) {
    LvglWin* w = (LvglWin*)lv_event_get_user_data(e);
    if (w && w->win) {
        lv_obj_delete(w->win);
        w->win = 0;
        w->content = 0;
        reg_del(w);
    }
}

static void win_min_cb(lv_event_t* e) {
    LvglWin* w = (LvglWin*)lv_event_get_user_data(e);
    lvgl_win_minimize(w);
}

static void win_max_cb(lv_event_t* e) {
    LvglWin* w = (LvglWin*)lv_event_get_user_data(e);
    lvgl_win_zoom_toggle(w);
}

LvglWin* lvgl_win_create(const char* title, int x, int y, int w, int h) {
    // Clamp window geometry to the screen work area, same policy as
    // WM::create_window. Without this every LVGL window (Settings 480x700,
    // Notepad 560px wide at a cascade offset, BIOS, ...) could extend past the
    // screen edge and become unreachable. Fullscreen-style windows (screen
    // sized at 0,0, e.g. boot splash / lock screen / BSOD) are exempt.
    Screen* s0 = platform_screen();
    const int REAL_W = s0 ? s0->width : 800;
    const int REAL_H = s0 ? s0->height : 600;
    const bool fullscreen_like = (x == 0 && y == 0 && w >= REAL_W && h >= REAL_H);
    {
        const int SCREEN_W = REAL_W;
        const int SCREEN_H = REAL_H;
        const int TASKBAR_H = 30;
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
    r->x = x; r->y = y; r->w = w; r->h = h;
    r->system_window = fullscreen_like;
    if (title) {
        strncpy(r->title, title, sizeof(r->title) - 1);
        r->title[sizeof(r->title) - 1] = 0;
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
    lv_obj_set_style_text_font(title_lbl, &nefu::lv_font_cjk_16, 0);

    // Close button on the right side of the header
    lv_obj_t* close_btn = lv_win_add_button(r->win, LV_SYMBOL_CLOSE, 34);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x3D4B66), 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xC23B3B), LV_STATE_PRESSED);
    lv_obj_add_event_cb(close_btn, win_close_cb, LV_EVENT_CLICKED, r);

    // Maximize/fullscreen button
    lv_obj_t* max_btn = lv_win_add_button(r->win, LV_SYMBOL_UP, 34);
    lv_obj_set_style_bg_color(max_btn, lv_color_hex(0x3D4B66), 0);
    lv_obj_set_style_bg_color(max_btn, lv_color_hex(0x4A9EFF), LV_STATE_PRESSED);
    lv_obj_add_event_cb(max_btn, win_max_cb, LV_EVENT_CLICKED, r);

    // Minimize ("shrink") button: hides the window and parks it in the taskbar
    lv_obj_t* min_btn = lv_win_add_button(r->win, LV_SYMBOL_DOWN, 34);
    lv_obj_set_style_bg_color(min_btn, lv_color_hex(0x3D4B66), 0);
    lv_obj_set_style_bg_color(min_btn, lv_color_hex(0x9ECE6E), LV_STATE_PRESSED);
    lv_obj_add_event_cb(min_btn, win_min_cb, LV_EVENT_CLICKED, r);

    // Content area: light panel, no padding inside
    r->content = lv_win_get_content(r->win);
    lv_obj_set_style_bg_color(r->content, lv_color_hex(0xEDF0F6), 0);
    lv_obj_set_style_bg_opa(r->content, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(r->content, 0, 0);

    r->open = true;
    reg_add(r);
    return r;
}

void lvgl_win_set_title(LvglWin* w, const char* t) {
    if (!w || !w->win || !t) return;
    lv_obj_t* hdr = lv_win_get_header(w->win);
    // first child label is the title we created
    lv_obj_t* lbl = lv_obj_get_child(hdr, 0);
    if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
        lv_label_set_text(lbl, t);
    }
    strncpy(w->title, t, sizeof(w->title) - 1);   // taskbar keeps its own copy
    w->title[sizeof(w->title) - 1] = 0;
}

void lvgl_win_close(LvglWin* w) {
    if (!w) return;
    if (w->win) {
        lv_obj_delete(w->win);
        w->win = 0;
        w->content = 0;
    }
    reg_del(w);
}

} // namespace nefu

