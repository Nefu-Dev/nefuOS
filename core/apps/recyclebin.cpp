// nefuOS Recycle Bin app
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../gui/widgets.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {

struct RecycleBinState {
    int w, h;
    int count;
};

static void recyclebin_paint(Window* win) {
    Surface& s = win->back;
    int W = win->content_w;
    int H = win->content_h;
    gfx::fillrect(s, 0, 0, W, H, 0x1e1e2e);

    RecycleBinState* st = (RecycleBinState*)win->userdata;

    gfx::text(s, 15, 15, "=== Recycle Bin ===", 0x89b4fa, 0);

    int y = 50;
    if (st->count == 0) {
        gfx::text(s, 50, y, "Recycle Bin is empty.", 0x6c7086, 0);
    } else {
        for (int i = 0; i < st->count && y < H - 50; i++) {
            char buf[32];
            ksprintf(buf, sizeof(buf), "Deleted file %d", i + 1);
            gfx::text(s, 25, y, buf, 0xff6b6b, 0);
            y += 22;
        }
    }

    // Buttons
    gfx::fillrect(s, 15, H - 35, 100, 25, 0xff6b6b);
    gfx::text(s, 35, H - 30, "Empty All", 0xffffff, 0);
}

static void recyclebin_on_mouse(Window* win, int mx, int my, uint8_t buttons) {
    RecycleBinState* st = (RecycleBinState*)win->userdata;
    if (!(buttons & 1)) return;

    int H = win->content_h;
    if (mx >= 15 && mx <= 115 && my >= H - 35 && my <= H - 10) {
        st->count = 0;
    }
}

void app_recyclebin_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Recycle Bin", x, y, 300, 250);
    if (!w) return;
    RecycleBinState* st = new RecycleBinState();
    st->w = w->content_w;
    st->h = w->content_h;
    st->count = 0;
    w->userdata = st;
    w->on_paint = recyclebin_paint;
    w->on_mouse = recyclebin_on_mouse;
    g_wm->raise(w);
}

} // namespace nefu
