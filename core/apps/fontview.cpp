// nefuOS Font Viewer - demonstrates scalable TrueType rendering
// powered by stb_truetype (MIT) with the embedded Montserrat-Medium font
// (OFL-1.1). Shows font metrics, a sample line at multiple sizes, and an
// ASCII character gallery. Falls back to a message when TTF is unavailable.
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/widgets.h"
#include "../gui/ttfont.h"

namespace nefu {

struct FontViewState {
    int y_off;      // scroll offset
    bool dragging;
    int drag_last;
};

static void fontview_paint(Window* w) {
    FontViewState* st = (FontViewState*)w->userdata;
    Surface& s = w->back;
    s.fill(color::WHITE);
    gfx::text(s, 14, 12, "Font Viewer - stb_truetype scalable text", color::BLUE, color::WHITE);
    if (!ttf_ready()) {
        gfx::text(s, 14, 48, "TTF engine not available (bitmap font only).", color::RED, color::WHITE);
        gfx::text(s, 14, 68, "Embedded font: VT323-Regular.ttf (OFL-1.1).", color::TEXT2, color::WHITE);
        return;
    }
    int y = 40 - st->y_off;
    char buf[160];
    gfx::text(s, 14, y, "Font    : VT323 (SIL OFL 1.1, Google Fonts)", color::TEXT2, color::WHITE); y += 16;
    gfx::text(s, 14, y, "Engine  : stb_truetype (MIT, nothings/stb)", color::TEXT2, color::WHITE); y += 16;
    gfx::text(s, 14, y, "Render  : vector outlines, anti-aliased", color::TEXT2, color::WHITE); y += 24;

    // sample sizes
    static const int sizes[] = { 12, 16, 24, 32, 48 };
    for (int i = 0; i < 5; i++) {
        ksprintf(buf, sizeof(buf), "Sample %dpx - nefuOS", sizes[i]);
        ttf_draw_text(&s, 14, y, buf, sizes[i], color::TEXT, color::WHITE);
        y += sizes[i] + 10;
        if (y > w->content_h + st->y_off) break;
    }
    y += 6;
    gfx::text(s, 14, y, "ASCII gallery (24px):", color::TEXT2, color::WHITE); y += 26;
    // printable ASCII 0x20..0x7E
    char row[64];
    int idx = 0;
    for (int c = 32; c <= 126; c++) {
        row[idx++] = (char)c;
        if (idx == 32) {
            row[idx] = 0;
            ttf_draw_text(&s, 14, y, row, 24, color::TEXT, color::WHITE);
            y += 26;
            idx = 0;
        }
    }
    if (idx > 0) {
        row[idx] = 0;
        ttf_draw_text(&s, 14, y, row, 24, color::TEXT, color::WHITE);
    }
    ksprintf(buf, sizeof(buf), "advance width check: 'nefuOS' = %d px at 16pt",
             ttf_text_width("nefuOS", 16));
    gfx::text(s, 14, 200, buf, color::TEXT2, color::WHITE);
}

static void fontview_mouse(Window* w, int mx, int my, uint8_t buttons) {
    FontViewState* st = (FontViewState*)w->userdata;
    bool pressed = buttons && !st->dragging;
    if (pressed) { st->dragging = true; st->drag_last = my; }
    if (!buttons && st->dragging) { st->dragging = false; }
    if (buttons && st->dragging) {
        int dy = my - st->drag_last;
        st->drag_last = my;
        st->y_off += dy;
        if (st->y_off < 0) st->y_off = 0;
    }
}

static void fontview_scroll(Window* w, int delta) {
    FontViewState* st = (FontViewState*)w->userdata;
    st->y_off -= delta * 20;
    if (st->y_off < 0) st->y_off = 0;
}

static void fontview_close(Window* w) {
    if (w->userdata) { delete (FontViewState*)w->userdata; w->userdata = 0; }
}

void fontview_launch() {
    ttf_init();
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Font Viewer", x, y, 420, 320);
    if (!w) return;
    FontViewState* st = new FontViewState();
    st->y_off = 0;
    st->dragging = false;
    w->userdata = st;
    w->on_paint = fontview_paint;
    w->on_mouse = fontview_mouse;
    w->on_scroll = fontview_scroll;
    w->on_close = fontview_close;
}

} // namespace nefu
