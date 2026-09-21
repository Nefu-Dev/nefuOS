// nefuOS Sticky Notes
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../vfs/vfs.h"

namespace nefu {

struct StickyState {
    int w, h;
    String text;
};

static void sticky_paint(Window* win) {
    StickyState* st = (StickyState*)win->userdata;
    Surface& cs = win->back;
    cs.fill(0x00FFFACD); // lemonchiffon yellow

    // Draw text lines
    int y = 10;
    int cols = (st->w - 20) / 8;
    if (cols < 1) cols = 20;

    const char* p = st->text.c_str();
    int line_start = 0;
    int len = (int)st->text.len();

    for (int i = 0; i <= len; i++) {
        if (p[i] == '\n' || i == len) {
            int line_len = i - line_start;
            if (line_len > cols) line_len = cols;
            char buf[128];
            int n = line_len < 127 ? line_len : 127;
            for (int k = 0; k < n; k++) buf[k] = p[line_start + k];
            buf[n] = 0;
            gfx::text(cs, 10, y, buf, 0x00333333, 0x00FFFACD);
            y += 16;
            line_start = i + 1;
            if (y > st->h - 30) break;
        }
    }

    // Bottom hint
    gfx::text(cs, 10, st->h - 20, "Type in terminal: sticky add 'text'", 0x00999966, 0x00FFFACD);
}

void sticky_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Sticky Note", x, y, 220, 180);
    if (!w) return;
    StickyState* st = new StickyState();
    st->w = w->content_w;
    st->h = w->content_h;
    st->text = "Welcome to nefuOS!\n\nThis is a sticky note.\n\nWrite your notes here.";
    w->userdata = st;
    w->on_paint = sticky_paint;
    g_wm->raise(w);
}

} // namespace nefu
