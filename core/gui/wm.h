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
};

struct Window {
    int x, y, w, h;
    bool visible;
    bool minimized;
    bool closed;
    bool dragging;
    int drag_off_x, drag_off_y;
    String title;
    Surface back;                    // content buffer（）
    int content_x, content_y;        // content screen coords
    int content_w, content_h;        // content size
    void (*on_paint)(Window* w);
    void (*on_key)(Window* w, const KeyEvent* e);
    void (*on_mouse)(Window* w, int mx, int my, uint8_t buttons);
    void (*on_scroll)(Window* w, int delta);
    void (*on_close)(Window* w);
    void (*on_drag)(Window* w, int mx, int my);   // content drag (highlight)
    void (*on_drop)(Window* w);                    // content drop (finish)
    void* userdata;
};

class WM {
public:
    WM();
    Window* create_window(const char* title, int x, int y, int w, int h);
    void close_window(Window* w);
    void cleanup();
    void paint_all(Surface& fb);
    void handle_mouse(int x, int y, uint8_t buttons);
    void handle_key(const KeyEvent* e);
    void handle_scroll(int delta);
    void raise(Window* w);
    Window* focus() { return focus_; }
    List<Window*>& windows() { return wins_; }
    Window* hit(int x, int y);
    int titlebar_h() const { return 18; }
private:
    void destroy_window(Window* w);
    List<Window*> wins_;
    Window* focus_;
    Window* drag_;
    Window* drag_win_;
    uint8_t last_buttons_;
};

extern WM* g_wm;

} // namespace nefu
