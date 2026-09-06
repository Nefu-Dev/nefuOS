// nefuOS 窗口管理器实现
#include "wm.h"

namespace nefu {

WM* g_wm = 0;

WM::WM() : focus_(0), drag_(0), last_buttons_(0) {
    drag_win_ = 0;
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
    win->content_y = y + 18;
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
    wins_.push(win);
    raise(win);
    return win;
}

void WM::destroy_window(Window* w) {
    if (!w) return;
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
    for (int i = 0; i < wins_.size(); i++) {
        Window* w = wins_[i];
        if (!w->visible || w->minimized || w->closed) continue;
        // 窗口边框
        gfx::rect(fb, w->x, w->y, w->w, w->h, color::BORDER);
        // 标题栏
        uint32_t bar = (w == focus_) ? color::BLUE : 0x006F7C8C;
        gfx::fillrect(fb, w->x + 1, w->y + 1, w->w - 2, 17, bar);
        gfx::text(fb, w->x + 4, w->y + 1, w->title.c_str(), color::WHITE, bar);
        // 关闭按钮
        int bx = w->x + w->w - 20, by = w->y + 3;
        gfx::fillrect(fb, bx, by, 16, 12, color::RED);
        gfx::line(fb, bx + 3, by + 2, bx + 12, by + 9, color::WHITE);
        gfx::line(fb, bx + 12, by + 2, bx + 3, by + 9, color::WHITE);
        // 内容区（先让应用重绘其状态，再合成上屏）
        w->content_x = w->x;
        w->content_y = w->y + 18;
        if (w->on_paint) w->on_paint(w);
        gfx::blit_clip(fb, w->back, w->content_x, w->content_y, 0, 0, w->content_w, w->content_h);
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

    // 拖动中
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
        drag_->content_y = ny + 18;
        return;
    }

    Window* win = hit(x, y);
    if (!win) return;

    if (pressed) {
        // 关闭按钮
        if (y >= win->y && y < win->y + 18 &&
            x >= win->x + win->w - 20 && x < win->x + win->w - 4) {
            close_window(win);
            return;
        }
        // 标题栏拖动
        if (y < win->y + 18) {
            raise(win);
            win->dragging = true;
            drag_ = win;
            win->drag_off_x = x - win->x;
            win->drag_off_y = y - win->y;
            return;
        }
        raise(win);
    }

    if (pressed && win->on_drag && y >= win->y + 18) {
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
