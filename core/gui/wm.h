// nefuOS window manager
#pragma once
#include "../klib/klib.h"
#include "gfx.h"

namespace nefu {

struct Window;
struct KeyEvent {
    int keycode;
    char ascii;
    bool down;
    char utf8[8];   // multi-byte UTF-8 for IME input (CJK etc.), empty for raw keys
    uint8_t mods;   // bit0=Shift bit1=Ctrl bit2=Alt (for Shift+/Ctrl+ shortcuts)
};

struct Window {
    int x, y, w, h;
    int prev_x, prev_y, prev_w, prev_h;  // restore when un-maximizing
    bool visible;
    bool minimized;
    bool maximized;
    bool closed;
    bool dragging;
    int drag_off_x, drag_off_y;
    int scroll_y;           // vertical scroll offset
    int content_scroll_h;   // total scrollable content height (auto-detected)
    bool app_scroll;        // app draws its own scrolled view (blit offset 0);
                            // scroll_y mirrors the app offset for the scrollbar
    String title;
    Surface back;                    // content buffer()
    int content_x, content_y;        // content screen coords
    int content_w, content_h;        // content size
    void (*on_paint)(Window* w);
    void (*on_tick)(Window* w);                 // periodic heartbeat (animation/game update)
    void (*on_key)(Window* w, const KeyEvent* e);
    void (*on_mouse)(Window* w, int mx, int my, uint8_t buttons);
    void (*on_scroll)(Window* w, int delta);
    void (*on_close)(Window* w);
    void (*on_drag)(Window* w, int mx, int my);   // content drag (highlight)
    void (*on_drop)(Window* w);                    // content drop (finish)
    bool esc_close;          // ESC auto-closes the window (default true; windows like BIOS that manage ESC themselves set false)
    bool fullscreen;         // Fullscreen window: fills the entire framebuffer, no title bar (BIOS/lock screen, etc.)
    bool fs_toggle;          // User-triggered fullscreen (F11): regular LVGL windows hide the title bar and fill the whole screen; F11/ESC restores
    void* userdata;
    struct LvglWin* lvw;      // LVGL window container (NULL = legacy fallback)
    void* lv_canvas;          // lv_obj_t* canvas backed by 'back'
};

class WM {
public:
    WM();
    static const int TASKBAR_H = 30;   // Bottom taskbar height: shared by maximize/fullscreen geometry calculations
    Window* create_window(const char* title, int x, int y, int w, int h, bool fullscreen = false);
    void close_window(Window* w);
    void cleanup();
    void paint_all(Surface& fb);
    void tick();                       // run on_tick of every visible window
    void handle_mouse(int x, int y, uint8_t buttons);
    void handle_key(const KeyEvent* e);
    void handle_scroll(int delta);
    void raise(Window* w);
    void minimize_all();                 // Win+D/Win+M: minimize every window (show desktop)
    void toggle_maximize(Window* w);
    void toggle_fullscreen(Window* w);   // F11 fullscreen/restore (hides the title bar, fills the whole screen)
    void cycle_focus(bool reverse);      // Ctrl+Tab / Ctrl+Shift+Tab: focus the next/previous open window
    Window* focus() { return focus_; }
    List<Window*>& windows() { return wins_; }
    Window* hit(int x, int y);
    int titlebar_h() const { return 18; }
private:
    void destroy_window(Window* w);
    void relayout(Window* w);
    List<Window*> wins_;
    Window* focus_;
    Window* drag_;
    Window* drag_win_;
    Window* scroll_drag_;    // window whose scrollbar thumb is being dragged
    int scroll_drag_off_;    // press offset from the thumb top, in pixels
    uint8_t last_buttons_;
};

extern WM* g_wm;

} // namespace nefu
