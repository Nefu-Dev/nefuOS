// nefuOS WM implementation
#include "wm.h"
#include "lvgl_win.h"

namespace nefu {

WM* g_wm = 0;

// LVGL close-button callback: routes to WM::close_window (which runs the app's
// on_close and marks the window closed; destroy happens on the next cleanup).
static void wm_lv_close_cb(lv_event_t* e) {
    Window* w = (Window*)lv_event_get_user_data(e);
    if (w && g_wm) g_wm->close_window(w);
}

WM::WM() : focus_(0), drag_(0), last_buttons_(0) {
    drag_win_ = 0;
}

// Real LVGL title-bar height (the lv_win header); falls back to 18 for the
// legacy (pre-LVGL) path so hit-testing and content offsets stay correct.
static int wm_hdr_h(Window* w) {
    if (w && w->lvw && w->lvw->win) {
        lv_obj_t* hdr = lv_win_get_header(w->lvw->win);
        int hh = lv_obj_get_height(hdr);
        if (hh > 8) return hh;
    }
    return 18;
}

Window* WM::create_window(const char* title, int x, int y, int w, int h) {
    if (w < 60 || h < 40) return 0;
    Window* win = new Window();
    if (!win) return 0;
    win->x = x; win->y = y; win->w = w; win->h = h;
    win->visible = true;
    win->minimized = false;
    win->closed = false;
    win->dragging = false;
    win->title = title ? title : "";
    win->content_w = w;
    win->content_h = h - 18;
    win->content_x = x;
    win->content_y = y + wm_hdr_h(win);
    win->on_paint = 0;
    win->on_key = 0;
    win->on_mouse = 0;
    win->on_scroll = 0;
    win->on_close = 0;
    win->userdata = 0;
    win->back.addr = (uint8_t*)kalloc((size_t)win->content_w * (size_t)win->content_h * 4);
    win->back.width = win->content_w;
    win->back.height = win->content_h;
    win->back.pitch = win->content_w * 4;
    if (!win->back.addr) { delete win; return 0; }
    win->back.fill(color::PANEL);
    win->lvw = 0;
    win->lv_canvas = 0;
    // Render the window through an LVGL lv_win container (title bar + close
    // button from LVGL, app content drawn into the canvas backed by back).
    LvglWin* lvw = new LvglWin();
    if (lvw) {
        memset(lvw, 0, sizeof(*lvw));
        lvw->win = lv_win_create(lv_screen_active());
        if (lvw->win) {
            lv_obj_set_size(lvw->win, w, h);
            lv_obj_set_pos(lvw->win, x, y);
            lv_obj_set_style_radius(lvw->win, 6, 0);
            lv_obj_set_style_border_color(lvw->win, lv_color_hex(0x3A4458), 0);
            lv_obj_set_style_border_width(lvw->win, 1, 0);
            lv_obj_set_style_shadow_width(lvw->win, 10, 0);
            lv_obj_set_style_shadow_opa(lvw->win, LV_OPA_40, 0);
            lv_obj_t* hdr = lv_win_get_header(lvw->win);
            lv_obj_set_style_bg_color(hdr, lv_color_hex(0x232B3D), 0);
            lv_obj_set_style_pad_left(hdr, 10, 0);
            lv_obj_t* tl = lv_label_create(hdr);
            lv_label_set_text(tl, title ? title : "");
            lv_obj_set_style_text_color(tl, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(tl, &lv_font_montserrat_14, 0);
            lv_obj_t* cb = lv_win_add_button(lvw->win, LV_SYMBOL_CLOSE, 34);
            lv_obj_set_style_bg_color(cb, lv_color_hex(0x3D4B66), 0);
            lv_obj_set_style_bg_color(cb, lv_color_hex(0xC23B3B), LV_STATE_PRESSED);
            lv_obj_add_event_cb(cb, wm_lv_close_cb, LV_EVENT_CLICKED, win);
            lvw->content = lv_win_get_content(lvw->win);
            lv_obj_set_style_bg_color(lvw->content, lv_color_hex(0xEDF0F6), 0);
            lv_obj_set_style_bg_opa(lvw->content, LV_OPA_COVER, 0);
            lv_obj_set_style_pad_all(lvw->content, 0, 0);
            lvw->open = true;
            lvw->userdata = win;
            win->lvw = lvw;
            win->lv_canvas = lv_canvas_create(lvw->content);
            lv_obj_set_size((lv_obj_t*)win->lv_canvas, win->content_w, win->content_h);
            lv_canvas_set_buffer((lv_obj_t*)win->lv_canvas, win->back.addr,
                                 win->content_w, win->content_h, LV_COLOR_FORMAT_ARGB8888);
        } else {
            delete lvw;
            lvw = 0;
        }
    }
    wins_.push(win);
    raise(win);
    return win;
}

void WM::destroy_window(Window* w) {
    if (!w) return;
    if (w->lvw && w->lvw->win) {
        lv_obj_delete(w->lvw->win);
        w->lvw->win = 0;
        w->lvw->content = 0;
    }
    if (w->lvw) { delete w->lvw; w->lvw = 0; }
    w->lv_canvas = 0;
    if (w->back.addr) kfree(w->back.addr);
    w->back.addr = 0;
    if (focus_ == w) focus_ = 0;
    if (drag_ == w) drag_ = 0;
    delete w;
}

void WM::close_window(Window* w) {
    if (!w || w->closed) return;
    if (w->on_close) w->on_close(w);
    w->closed = true;
    if (w->lvw && w->lvw->win) lv_obj_add_flag(w->lvw->win, LV_OBJ_FLAG_HIDDEN);
    if (focus_ == w) focus_ = 0;
    if (drag_ == w) drag_ = 0;
}

void WM::cleanup() {
    int i = 0;
    while (i < wins_.size()) {
        if (wins_[i]->closed) {
            destroy_window(wins_[i]);
            wins_.remove(i);
        } else i++;
    }
    if (focus_ && focus_->closed) focus_ = 0;
}

Window* WM::hit(int x, int y) {
    for (int i = wins_.size() - 1; i >= 0; i--) {
        Window* w = wins_[i];
        if (!w->visible || w->minimized || w->closed) continue;
        if (x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h) return w;
    }
    return 0;
}

void WM::raise(Window* w) {
    if (!w) return;
    for (int i = 0; i < wins_.size(); i++) {
        if (wins_[i] == w) {
            wins_.remove(i);
            break;
        }
    }
    wins_.push(w);
    focus_ = w;
}

void WM::paint_all(Surface& fb) {
    (void)fb;
    for (int i = 0; i < wins_.size(); i++) {
        Window* w = wins_[i];
        if (!w->visible || w->minimized || w->closed) continue;
        // LVGL owns the frame (title bar + close button). The app repaints its
        // content into the canvas (backed by w->back); invalidating the canvas
        // makes LVGL flush it to the desktop in the next refresh.
        w->content_x = w->x;
        w->content_y = w->y + wm_hdr_h(w);
        if (w->on_paint) w->on_paint(w);
        if (w->lv_canvas) lv_obj_invalidate((lv_obj_t*)w->lv_canvas);
        // keep the LVGL window position in sync with drag
        if (w->lvw && w->lvw->win) {
            lv_obj_set_pos(w->lvw->win, w->x, w->y);
        }
    }
}

void WM::handle_mouse(int x, int y, uint8_t buttons) {
    bool pressed = (buttons != 0) && (last_buttons_ == 0);
    bool released = (buttons == 0) && (last_buttons_ != 0);
    last_buttons_ = buttons;

    // content drag (file drag & drop)
    if (drag_win_ && !released) {
        if (drag_win_->on_drag) {
            int dx = x - drag_win_->content_x;
            int dy = y - drag_win_->content_y;
            drag_win_->on_drag(drag_win_, dx, dy);
        }
    }
    if (drag_win_ && released) {
        Window* dw = drag_win_;
        drag_win_ = 0;
        if (dw->on_drop) dw->on_drop(dw);
    }

    // dragging
    if (drag_) {
        if (released) {
            drag_->dragging = false;
            drag_ = 0;
            return;
        }
        int nx = x - drag_->drag_off_x;
        int ny = y - drag_->drag_off_y;
        Screen* sc = platform_screen();
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (nx + drag_->w > sc->width) nx = sc->width - drag_->w;
        if (ny + drag_->h > sc->height) ny = sc->height - drag_->h;
        drag_->x = nx;
        drag_->y = ny;
        drag_->content_x = nx;
        drag_->content_y = ny + wm_hdr_h(drag_);
        return;
    }

    Window* win = hit(x, y);
    if (!win) return;

    int hdr = wm_hdr_h(win);

    if (pressed) {

        if (y >= win->y && y < win->y + hdr &&
            x >= win->x + win->w - 40 && x < win->x + win->w - 4) {
            close_window(win);
            return;
        }

        if (y < win->y + hdr) {
            raise(win);
            win->dragging = true;
            drag_ = win;
            win->drag_off_x = x - win->x;
            win->drag_off_y = y - win->y;
            return;
        }
        raise(win);
    }

    if (pressed && win->on_drag && y >= win->y + hdr) {
        drag_win_ = win;   // start content drag
    }

    if (win->on_mouse && !win->dragging) {
        win->on_mouse(win, x - win->content_x, y - win->content_y, buttons);
    }
}

void WM::handle_key(const KeyEvent* e) {
    if (focus_ && focus_->on_key) focus_->on_key(focus_, e);
}

void WM::handle_scroll(int delta) {
    if (focus_ && focus_->on_scroll) focus_->on_scroll(focus_, delta);
}

} // namespace nefu
