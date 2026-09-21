// Stub functions for removed apps
// These are simple placeholders until the apps are rewritten
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../gui/widgets.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {

struct StubState {
    int w, h;
    const char* name;
};

static void stub_paint(Window* win) {
    Surface& s = win->back;
    int W = win->content_w;
    int H = win->content_h;
    gfx::fillrect(s, 0, 0, W, H, 0x1e1e2e);

    StubState* st = (StubState*)win->userdata;
    gfx::text(s, 20, 40, st->name, 0x89b4fa, 0);
    gfx::text(s, 20, 80, "This app is under construction.", 0xcdd6f4, 0);
    gfx::text(s, 20, 105, "It will be available in future versions.", 0x6c7086, 0);
}

static Window* create_stub_window(const char* name, int w, int h) {
    int x, y;
    cascade_pos(&x, &y);
    Window* win = g_wm->create_window(name, x, y, w, h);
    if (!win) return 0;
    StubState* st = new StubState();
    st->w = win->content_w;
    st->h = win->content_h;
    st->name = name;
    win->userdata = st;
    win->on_paint = stub_paint;
    g_wm->raise(win);
    return win;
}

void app_screenshot_launch() {
    create_stub_window("Screenshot", 300, 180);
}

void app_colorpicker_launch() {
    create_stub_window("Color Picker", 300, 180);
}

void app_search_launch() {
    create_stub_window("File Search", 350, 250);
}

void app_recyclebin_launch() {
    create_stub_window("Recycle Bin", 400, 300);
}

} // namespace nefu
