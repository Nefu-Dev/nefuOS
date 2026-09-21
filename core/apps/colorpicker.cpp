// nefuOS Color Picker app
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../gui/widgets.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {

struct ColorPickerState {
    int w, h;
    uint32_t color;
};

static void colorpicker_paint(Window* win) {
    Surface& s = win->back;
    int W = win->content_w;
    int H = win->content_h;
    gfx::fillrect(s, 0, 0, W, H, 0x1e1e2e);

    ColorPickerState* st = (ColorPickerState*)win->userdata;

    gfx::text(s, 15, 15, "=== Color Picker ===", 0x89b4fa, 0);

    // Show current color
    gfx::text(s, 15, 45, "Current:", 0xcdd6f4, 0);
    gfx::fillrect(s, 15, 65, 80, 50, st->color);

    char buf[32];
    ksprintf(buf, sizeof(buf), "#%06X", st->color & 0xFFFFFF);
    gfx::text(s, 110, 80, buf, 0xa6e3a1, 0);

    // Color palette
    gfx::text(s, 15, 130, "Palette:", 0xcdd6f4, 0);

    uint32_t colors[] = {
        0xff0000, 0xff7f00, 0xffff00, 0x00ff00, 0x0000ff, 0x8b00ff,
        0xffffff, 0x808080, 0x000000, 0xffc0cb, 0xa52a2a, 0x808000,
    };
    for (int i = 0; i < 12; i++) {
        int px = 15 + (i % 6) * 32;
        int py = 155 + (i / 6) * 32;
        gfx::fillrect(s, px, py, 28, 25, colors[i]);
        if (st->color == colors[i]) {
            gfx::rect(s, px-1, py-1, 30, 27, 0xffffff);
        }
    }
}

static void colorpicker_on_mouse(Window* win, int mx, int my, uint8_t buttons) {
    ColorPickerState* st = (ColorPickerState*)win->userdata;
    if (!(buttons & 1)) return;

    uint32_t colors[] = {
        0xff0000, 0xff7f00, 0xffff00, 0x00ff00, 0x0000ff, 0x8b00ff,
        0xffffff, 0x808080, 0x000000, 0xffc0cb, 0xa52a2a, 0x808000,
    };
    for (int i = 0; i < 12; i++) {
        int px = 15 + (i % 6) * 32;
        int py = 155 + (i / 6) * 32;
        if (mx >= px && mx <= px+28 && my >= py && my <= py+25) {
            st->color = colors[i];
            return;
        }
    }
}

void app_colorpicker_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Color Picker", x, y, 260, 250);
    if (!w) return;
    ColorPickerState* st = new ColorPickerState();
    st->w = w->content_w;
    st->h = w->content_h;
    st->color = 0x4a9eff;
    w->userdata = st;
    w->on_paint = colorpicker_paint;
    w->on_mouse = colorpicker_on_mouse;
    g_wm->raise(w);
}

} // namespace nefu
