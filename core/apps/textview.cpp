// nefuOS 文本查看器
#include "apps.h"
#include "../gui/gfx.h"
#include "../platform.h"

namespace nefu {

struct TVState {
    FSNode* file;
    int scroll;
};

static void tv_paint(Window* w) {
    TVState* st = (TVState*)w->userdata;
    Surface& s = w->back;
    s.fill(color::WHITE);
    List<String> lines;
    file_to_lines(st->file, lines, 100000);
    int vis = s.height / 16;
    if (st->scroll > lines.size() - vis && lines.size() - vis > 0) st->scroll = lines.size() - vis;
    for (int i = st->scroll; i < lines.size() && i < st->scroll + vis; i++) {
        gfx::text(s, 4, (i - st->scroll) * 16, lines[i].c_str(), color::TEXT, color::WHITE);
    }
    if (lines.empty()) {
        gfx::text(s, 4, 4, "(empty file)", color::TEXT2, color::WHITE);
    }
}

static void tv_scroll(Window* w, int delta) {
    TVState* st = (TVState*)w->userdata;
    st->scroll += delta > 0 ? -2 : 2;
    if (st->scroll < 0) st->scroll = 0;
}

static void tv_key(Window* w, const KeyEvent* e) {
    TVState* st = (TVState*)w->userdata;
    if (!e->down) return;
    if (e->keycode == KEY_UP) st->scroll -= 1;
    else if (e->keycode == KEY_DOWN) st->scroll += 1;
    else if (e->keycode == KEY_PGUP) st->scroll -= 12;
    else if (e->keycode == KEY_PGDN) st->scroll += 12;
    else if (e->keycode == KEY_HOME) st->scroll = 0;
    else if (e->keycode == KEY_END) st->scroll = 1 << 30;
    if (st->scroll < 0) st->scroll = 0;
}

static void tv_close(Window* w) {
    if (w->userdata) delete (TVState*)w->userdata;
    w->userdata = 0;
}

void app_show_textview(FSNode* file) {
    if (!file || file->is_dir) return;
    int x, y;
    cascade_pos(&x, &y);
    String title = file->name;
    title += " - Text Viewer";
    Window* w = g_wm->create_window(title.c_str(), x, y, 560, 360);
    if (!w) return;
    TVState* st = new TVState();
    st->file = file;
    st->scroll = 0;
    w->userdata = st;
    w->on_paint = tv_paint;
    w->on_scroll = tv_scroll;
    w->on_key = tv_key;
    w->on_close = tv_close;
}

} // namespace nefu
