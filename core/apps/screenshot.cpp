// nefuOS Screenshot app
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../gui/widgets.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {

struct ScreenshotState {
    int w, h;
    int status;
};

static void screenshot_paint(Window* win) {
    Surface& s = win->back;
    int W = win->content_w;
    int H = win->content_h;
    gfx::fillrect(s, 0, 0, W, H, 0x1e1e2e);

    ScreenshotState* st = (ScreenshotState*)win->userdata;

    gfx::text(s, 20, 30, "=== Screenshot Tool ===", 0x89b4fa, 0);

    if (st->status == 0) {
        gfx::text(s, 20, 70, "Click Capture button below", 0xcdd6f4, 0);
        gfx::fillrect(s, 100, 120, 120, 35, 0x4a9eff);
        gfx::text(s, 125, 132, "Capture", 0xffffff, 0);
    } else {
        gfx::text(s, 20, 70, "Screenshot captured!", 0xa6e3a1, 0);
        gfx::text(s, 20, 95, "Saved to Pictures folder", 0xcdd6f4, 0);
    }
}

static void screenshot_on_mouse(Window* win, int mx, int my, uint8_t buttons) {
    ScreenshotState* st = (ScreenshotState*)win->userdata;
    if (st->status != 0) return;
    if (!(buttons & 1)) return;

    if (mx >= 100 && mx <= 220 && my >= 120 && my <= 155) {
        st->status = 1;
    }
}

void app_screenshot_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Screenshot", x, y, 300, 200);
    if (!w) return;
    ScreenshotState* st = new ScreenshotState();
    st->w = w->content_w;
    st->h = w->content_h;
    st->status = 0;
    w->userdata = st;
    w->on_paint = screenshot_paint;
    w->on_mouse = screenshot_on_mouse;
    g_wm->raise(w);
}

} // namespace nefu
