// nefuOS WM implementation
#include "wm.h"
#include "lv_cjk_font.h"
#include "../klib/klib.h"
#include "../platform.h"
#include "lvgl_win.h"
#include "desktop.h"   // start_menu_visible / start_menu_rect

namespace nefu {

// Blit window content around the open Start menu so the menu always floats
// above windows: the destination is split vertically around the menu rect.
static void blit_except_menu(Surface& fb, Surface& back,
                             int dx, int dy, int sx, int sy, int w, int h) {
    int mx1, my1, mx2, my2;
    start_menu_rect(&mx1, &my1, &mx2, &my2);
    if (my1 >= dy + h || my2 < dy || mx1 >= dx + w || mx2 < dx) {
        gfx::blit_clip(fb, back, dx, dy, sx, sy, w, h);
        return;
    }
    int y0 = dy, y1 = dy + h;
    int cut0 = my1, cut1 = my2 + 1;
    if (cut0 > y0)
        gfx::blit_clip(fb, back, dx, y0, sx, sy + (y0 - dy), w, cut0 - y0);
    if (y1 > cut1)
        gfx::blit_clip(fb, back, dx, cut1, sx, sy + (cut1 - dy), w, y1 - cut1);
}

// True when a rectangle overlaps the open Start menu (used to hide window
// scrollbars that would otherwise draw over the menu).
static bool overlaps_menu(int x, int y, int w, int h) {
    int mx1, my1, mx2, my2;
    start_menu_rect(&mx1, &my1, &mx2, &my2);
    return !(my1 >= y + h || my2 < y || mx1 >= x + w || mx2 < x);
}

WM* g_wm = 0;

// LVGL minimize-button callback: hide the window
static void wm_lv_min_cb(lv_event_t* e) {
    Window* win = (Window*)lv_event_get_user_data(e);
    if (!win) return;
    win->minimized = true;
    if (win->lvw && win->lvw->win) {
        lv_obj_add_flag(win->lvw->win, LV_OBJ_FLAG_HIDDEN);
    }
}

// LVGL maximize-button callback: toggle maximize (fit the work area above the
// taskbar, title bar stays). Single source of truth: WM::toggle_maximize, so
// the button and title-bar double-click behave identically.
static void wm_lv_max_cb(lv_event_t* e) {
    Window* win = (Window*)lv_event_get_user_data(e);
    if (win && g_wm) g_wm->toggle_maximize(win);
}

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
    if (w && (w->fullscreen || w->fs_toggle)) return 0;   // fullscreen windows have no title bar
    if (w && w->lvw && w->lvw->win) {
        lv_obj_t* hdr = lv_win_get_header(w->lvw->win);
        int hh = lv_obj_get_height(hdr);
        if (hh > 8) return hh;
    }
    return 18;
}

Window* WM::create_window(const char* title, int x, int y, int w, int h, bool fullscreen) {
    if (w < 60 || h < 40) return 0;

    if (!fullscreen) {
        // Clamp window size to screen bounds
        const int SCREEN_W = 800;
        const int SCREEN_H = 600;
        const int TASKBAR_H = 30;
        const int MAX_W = SCREEN_W - 8;
        const int MAX_H = SCREEN_H - TASKBAR_H - 8;

        if (w > MAX_W) w = MAX_W;
        if (h > MAX_H) h = MAX_H;

        // Clamp position
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x + w > SCREEN_W) x = SCREEN_W - w;
        if (y + h > SCREEN_H - TASKBAR_H) y = SCREEN_H - TASKBAR_H - h;
    }

    Window* win = new Window();
    if (!win) return 0;
    win->x = x; win->y = y; win->w = w; win->h = h;
    win->prev_x = x; win->prev_y = y; win->prev_w = w; win->prev_h = h;
    win->visible = true;
    win->minimized = false;
    win->maximized = false;
    win->closed = false;
    win->dragging = false;
    win->scroll_y = 0;
    win->content_scroll_h = 0;
    win->title = title ? title : "";
    win->content_w = w;
    win->content_h = fullscreen ? h : h - 18;
    win->content_x = x;
    win->content_y = fullscreen ? y : y + wm_hdr_h(win);
    win->on_paint = 0;
    win->on_tick = 0;
    win->on_key = 0;
    win->on_mouse = 0;
    win->on_scroll = 0;
    win->on_close = 0;
    win->esc_close = true;   // default: ESC closes the focused window
    win->fullscreen = fullscreen;
    win->fs_toggle = false;
    win->userdata = 0;
    win->back.addr = 0;
    win->lvw = 0;
    win->lv_canvas = 0;

    // Fullscreen windows (BIOS/lock screen, etc.): skip the LVGL container and
    // allocate a whole-frame content buffer; no title bar or scrollbar, content fills the framebuffer.
    if (fullscreen) {
        win->back.addr = (uint8_t*)kalloc((size_t)w * (size_t)h * 4);
        if (!win->back.addr) { delete win; return 0; }
        win->back.width = w;
        win->back.height = h;
        win->back.pitch = w * 4;
        win->back.fill(color::PANEL);
        wins_.push(win);
        raise(win);
        return win;
    }

    // Render the window through an LVGL lv_win container (title bar + close
    // button from LVGL, app content drawn into the canvas backed by back).
    LvglWin* lvw = new LvglWin();
    if (lvw) {
        memset(lvw, 0, sizeof(*lvw));
        lvw->win = lv_win_create(lv_screen_active());
        if (lvw->win) {
            lv_obj_set_size(lvw->win, w, h);
            lv_obj_set_pos(lvw->win, x, y);
            // LVGL handles dragging, WM syncs position
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
            // Minimize button
            lv_obj_t* minbtn = lv_win_add_button(lvw->win, LV_SYMBOL_MINUS, 28);
            lv_obj_set_style_bg_color(minbtn, lv_color_hex(0x3D4B66), 0);
            lv_obj_set_style_bg_color(minbtn, lv_color_hex(0x2E7D32), LV_STATE_PRESSED);
            lv_obj_add_event_cb(minbtn, wm_lv_min_cb, LV_EVENT_PRESSED, win);
            // Maximize button
            lv_obj_t* maxbtn = lv_win_add_button(lvw->win, LV_SYMBOL_UP, 28);
            lv_obj_set_style_bg_color(maxbtn, lv_color_hex(0x3D4B66), 0);
            lv_obj_set_style_bg_color(maxbtn, lv_color_hex(0x1565C0), LV_STATE_PRESSED);
            lv_obj_add_event_cb(maxbtn, wm_lv_max_cb, LV_EVENT_PRESSED, win);
            // Close button
            lv_obj_t* cb = lv_win_add_button(lvw->win, LV_SYMBOL_CLOSE, 28);
            lv_obj_set_style_bg_color(cb, lv_color_hex(0x3D4B66), 0);
            lv_obj_set_style_bg_color(cb, lv_color_hex(0xC23B3B), LV_STATE_PRESSED);
            lv_obj_add_event_cb(cb, wm_lv_close_cb, LV_EVENT_PRESSED, win);
            lvw->content = lv_win_get_content(lvw->win);
            lv_obj_set_style_bg_color(lvw->content, lv_color_hex(0xEDF0F6), 0);
            lv_obj_set_style_bg_opa(lvw->content, LV_OPA_COVER, 0);
            lv_obj_set_style_pad_all(lvw->content, 0, 0);
            // The LVGL header is taller than the legacy 18px assumption;
            // size the canvas to the REAL content area so app content
            // (e.g. wiki bottom buttons) is not clipped and stays clickable.
            int hh = lv_obj_get_height(hdr);
            if (hh <= 8) hh = 18;
            win->content_h = h - hh;
            if (win->content_h < 20) win->content_h = 20;
            win->back.addr = (uint8_t*)kalloc((size_t)win->content_w * (size_t)win->content_h * 4);
            win->back.width = win->content_w;
            win->back.height = win->content_h;
            win->back.pitch = win->content_w * 4;
            win->back.fill(color::PANEL);
            if (!win->back.addr) { delete lvw; lvw = 0; delete win; return 0; }
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
    klogf("wm: close window %s\n", w->title.c_str());
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

void WM::tick() {
    // run the heartbeat of every visible window (animation / game update)
    for (int i = 0; i < wins_.size(); i++) {
        Window* w = wins_[i];
        if (!w->visible || w->minimized || w->closed) continue;
        if (w->on_tick) w->on_tick(w);
    }
}

void WM::paint_all(Surface& fb) {
    for (int i = 0; i < wins_.size(); i++) {
        Window* w = wins_[i];
        if (!w->visible || w->minimized || w->closed) continue;
        // LVGL owns the frame (title bar + close button). The app content is
        // drawn by on_paint into w->back and blitted directly onto the
        // framebuffer over the LVGL content area (canvas rendering proved
        // unreliable on this LVGL build, so we bypass it entirely).
        w->content_x = w->x;
        int hh = wm_hdr_h(w);
        w->content_y = w->y + hh;
        // keep the canvas height in sync with the REAL header height once
        // LVGL has laid the window out (create_window measured too early
        // and got the default header size). Without this, app content at
        // the bottom (e.g. wiki buttons) is clipped and unclickable.
        if (hh > 18) {
            int nh = w->h - hh;
            if (nh > 20 && nh != w->content_h) {
                w->content_h = nh;
                w->back.height = nh;
                if (w->lv_canvas) lv_obj_set_size((lv_obj_t*)w->lv_canvas, w->content_w, nh);
            }
        }
        if (w->on_paint) w->on_paint(w);
        if (w->back.addr && w->content_w > 0 && w->content_h > 0) {
            // Apply scroll offset: blit from scroll_y position in back buffer
            int sy = w->scroll_y;
            if (sy < 0) sy = 0;
            if (sy > w->back.height - w->content_h) sy = w->back.height - w->content_h;
            if (sy < 0) sy = 0;
            blit_except_menu(fb, w->back, w->content_x, w->content_y,
                             0, sy, w->content_w, w->content_h);
        }
        // Sync LVGL window position: read LVGL pos back to WM
        if (w->lvw && w->lvw->win) {
            lv_coord_t lv_x = lv_obj_get_x(w->lvw->win);
            lv_coord_t lv_y = lv_obj_get_y(w->lvw->win);
            if (lv_x != w->x || lv_y != w->y) {
                // LVGL moved the window (e.g. by header drag). In user
                // fullscreen the header is hidden (no drag) and the WM owns
                // the geometry; do not let a stale LVGL coord pull it back.
                if (!w->fs_toggle) {
                    // Keep the window inside the 800x600 work area: a window
                    // dragged past the edge would otherwise become unreachable.
                    int MAX_X = 800 - w->w; if (MAX_X < 0) MAX_X = 0;
                    int MAX_Y = 600 - WM::TASKBAR_H - w->h; if (MAX_Y < 0) MAX_Y = 0;
                    if (lv_x < 0) lv_x = 0;
                    if (lv_y < 0) lv_y = 0;
                    if (lv_x > MAX_X) lv_x = MAX_X;
                    if (lv_y > MAX_Y) lv_y = MAX_Y;
                    w->x = lv_x;
                    w->y = lv_y;
                    w->content_x = w->x;
        // TEMP-FS: dump geometry ground truth (remove after diagnosis)
        if (w->lvw && w->lvw->win) {
            static int s_dbg_cnt = 0;
            if (s_dbg_cnt < 30) {
                s_dbg_cnt++;
                klogf("FSDBG2 i=%d wx=%d wy=%d lvx=%d lvy=%d scrx=%d scry=%d\n",
                       i, w->x, w->y,
                       (int)lv_obj_get_x(w->lvw->win), (int)lv_obj_get_y(w->lvw->win),
                       (int)lv_obj_get_scroll_x(lv_screen_active()),
                       (int)lv_obj_get_scroll_y(lv_screen_active()));
            }
        }
                    w->content_y = w->y + wm_hdr_h(w);
                    // Snap the LVGL window back inside the screen after a drag
                    // (only when the clamp above actually changed the position).
                    if (lv_obj_get_x(w->lvw->win) != lv_x || lv_obj_get_y(w->lvw->win) != lv_y)
                        lv_obj_set_pos(w->lvw->win, lv_x, lv_y);
                }
            }
        }
        if (w->fullscreen) continue;   // fullscreen windows have no scrollbar
        
        // Draw vertical scrollbar (hidden while the Start menu covers it)
        int sb_x = w->content_x + w->content_w - 8;
        int sb_y = w->content_y;
        int sb_h = w->content_h;
        int sb_w = 6;
        if (!overlaps_menu(sb_x, sb_y, sb_w, sb_h)) {
            // Scrollbar track
            gfx::fillrect(fb, sb_x, sb_y, sb_w, sb_h, 0xE0E0E0);
            
            // Scrollbar thumb (if content is scrollable)
            if (w->content_scroll_h > w->content_h) {
                int thumb_h = (w->content_h * w->content_h) / w->content_scroll_h;
                if (thumb_h < 20) thumb_h = 20;
                int thumb_y = sb_y + (w->scroll_y * (sb_h - thumb_h)) / (w->content_scroll_h - w->content_h);
                gfx::fillrect(fb, sb_x, thumb_y, sb_w, thumb_h, 0x888888);
            }
        }
    }
}

static uint32_t s_last_title_click = 0;
static int s_last_title_x = -9999, s_last_title_y = -9999;

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

        // Close button hit area - LVGL places X at rightmost, only ~30px wide
        // We let LVGL handle the button clicks, just need to avoid double-close
        // Only trigger close if we're way over on the right (LVGL handles its own buttons)
        // Actually, LVGL buttons handle their own clicks, so we DON'T need this here!

        if (y < win->y + hdr) {
            // double-click on the title bar toggles maximize/restore
            uint32_t now = platform_tick_ms();
            bool dbl = (now - s_last_title_click < 400) &&
                       (x - s_last_title_x < 10 && x - s_last_title_x > -10) &&
                       (y - s_last_title_y < 10 && y - s_last_title_y > -10);
            s_last_title_click = now;
            s_last_title_x = x;
            s_last_title_y = y;
            if (dbl) {
                s_last_title_click = 0;
                toggle_maximize(win);
                return;
            }
            raise(win);
            if (!win->maximized) {
                win->dragging = true;
                drag_ = win;
                win->drag_off_x = x - win->x;
                win->drag_off_y = y - win->y;
            }
            return;
        }
        raise(win);
    }

    // Scrollbar click handling
    if (pressed && win->content_scroll_h > win->content_h) {
        int sb_x = win->content_x + win->content_w - 8;
        int sb_y = win->content_y;
        int sb_h = win->content_h;
        
        if (x >= sb_x - 4 && x <= sb_x + 10 && y >= sb_y && y <= sb_y + sb_h) {
            // Click on scrollbar - jump to position
            int thumb_h = (win->content_h * win->content_h) / win->content_scroll_h;
            if (thumb_h < 20) thumb_h = 20;
            int thumb_y = sb_y + (win->scroll_y * (sb_h - thumb_h)) / (win->content_scroll_h - win->content_h);
            
            int click_y = y;
            if (click_y < thumb_y) {
                // Click above thumb - scroll up one page
                win->scroll_y -= win->content_h;
            } else if (click_y > thumb_y + thumb_h) {
                // Click below thumb - scroll down one page
                win->scroll_y += win->content_h;
            }
            
            if (win->scroll_y < 0) win->scroll_y = 0;
            int max_scroll = win->content_scroll_h - win->content_h;
            if (win->scroll_y > max_scroll) win->scroll_y = max_scroll;
            
            if (win->on_scroll) win->on_scroll(win, 0);
            return;
        }
    }

    if (pressed && win->on_drag && y >= win->y + hdr) {
        drag_win_ = win;   // start content drag
    }

    if (win->on_mouse && !win->dragging) {
        win->on_mouse(win, x - win->content_x, y - win->content_y, buttons);
    }
}

void WM::handle_key(const KeyEvent* e) {
    // global shortcut: F11 toggles between true fullscreen and windowed mode (except BIOS/lock screen)
    if (e && e->down && e->keycode == KEY_F11 && focus_ && !focus_->closed && !focus_->fullscreen) {
        toggle_fullscreen(focus_);
        return;
    }
    // while fullscreen, ESC exits fullscreen first instead of closing the window
    if (e && e->down && e->keycode == KEY_ESC && focus_ && focus_->fs_toggle) {
        toggle_fullscreen(focus_);
        return;
    }
    if (focus_ && focus_->on_key) focus_->on_key(focus_, e);
    // ESC closes the focused window as a keyboard fallback for the X button.
    // Windows that manage ESC themselves (e.g. the BIOS setup utility) set
    // esc_close = false so the key is fully handled by their on_key handler.
    if (e && e->down && e->keycode == KEY_ESC && focus_ && !focus_->closed && focus_->esc_close) {
        close_window(focus_);
    }
}

void WM::handle_scroll(int delta) {
    if (!focus_) return;
    
    // Auto-scroll: apply delta to window scroll offset
    focus_->scroll_y -= delta * 20;
    if (focus_->scroll_y < 0) focus_->scroll_y = 0;
    
    // Let the app handle it too (optional)
    if (focus_->on_scroll) focus_->on_scroll(focus_, delta);
}

void WM::relayout(Window* w) {
    if (!w) return;
    w->content_x = w->x;
    w->content_y = w->y + wm_hdr_h(w);
    int new_cw = w->w;
    int new_ch = w->h - wm_hdr_h(w);
    if (new_ch < 20) new_ch = 20;
    if (new_cw != w->content_w || new_ch != w->content_h) {
        if (w->back.addr) kfree(w->back.addr);
        w->back.addr = (uint8_t*)kalloc((size_t)new_cw * (size_t)new_ch * 4);
        w->back.width = new_cw;
        w->back.height = new_ch;
        w->back.pitch = new_cw * 4;
        w->content_w = new_cw;
        w->content_h = new_ch;
        w->back.fill(color::PANEL);
        if (w->lvw && w->lv_canvas) {
            lv_obj_set_size((lv_obj_t*)w->lv_canvas, new_cw, new_ch);
            lv_canvas_set_buffer((lv_obj_t*)w->lv_canvas, w->back.addr,
                                 new_cw, new_ch, LV_COLOR_FORMAT_ARGB8888);
        }
    }
    if (w->lvw && w->lvw->win) {
        lv_obj_set_pos(w->lvw->win, w->x, w->y);
        lv_obj_set_size(w->lvw->win, w->w, w->h);
        // apply the new geometry immediately: lv_obj_set_pos/size only update
        // the pending values; without a layout pass LVGL still reports (and
        // renders) the old coords for one frame.
        lv_obj_update_layout(w->lvw->win);
    }
}

void WM::toggle_maximize(Window* w) {
    if (!w) return;
    Screen* sc = platform_screen();
    if (!sc) return;
    if (!w->maximized) {
        w->prev_x = w->x;
        w->prev_y = w->y;
        w->prev_w = w->w;
        w->prev_h = w->h;
        w->x = 0;
        w->y = 0;
        // maximize = fill the workspace above the taskbar (title bar kept)
        w->w = sc->width;
        w->h = sc->height - WM::TASKBAR_H;
        if (w->h < 40) w->h = 40;
        w->maximized = true;
    } else {
        w->x = w->prev_x;
        w->y = w->prev_y;
        w->w = w->prev_w;
        w->h = w->prev_h;
        w->maximized = false;
    }
    relayout(w);
    if (w->on_paint) w->on_paint(w);
}

// True fullscreen: hide the LVGL title bar and fill the entire screen (including the taskbar area).
// F11 toggles it; ESC exits fullscreen. Windows created with fullscreen=true (BIOS,
// lock screen) manage the screen themselves and do not accept this toggle.
void WM::toggle_fullscreen(Window* w) {
    if (!w || w->fullscreen) return;
    Screen* sc = platform_screen();
    if (!sc) return;

    if (!w->fs_toggle) {
        // if already maximized, restore first, then save the restored geometry as the exit target,
        // so F11 exits back to the normal window state before entering fullscreen.
        if (w->maximized) {
            w->x = w->prev_x; w->y = w->prev_y; w->w = w->prev_w; w->h = w->prev_h;
            w->maximized = false;
        }
        w->prev_x = w->x; w->prev_y = w->y; w->prev_w = w->w; w->prev_h = w->h;
        w->x = 0; w->y = 0;
        w->w = sc->width;
        w->h = sc->height;
        w->fs_toggle = true;
        if (w->lvw && w->lvw->win) {
            lv_obj_t* hdr = lv_win_get_header(w->lvw->win);
            if (hdr) lv_obj_add_flag(hdr, LV_OBJ_FLAG_HIDDEN);
        }
        raise(w);   // raise the fullscreen window to the top
    } else {
        w->x = w->prev_x; w->y = w->prev_y; w->w = w->prev_w; w->h = w->prev_h;
        w->fs_toggle = false;
        if (w->lvw && w->lvw->win) {
            lv_obj_t* hdr = lv_win_get_header(w->lvw->win);
            if (hdr) lv_obj_remove_flag(hdr, LV_OBJ_FLAG_HIDDEN);
        }
    }
    relayout(w);
    if (w->on_paint) w->on_paint(w);
}

} // namespace nefu
