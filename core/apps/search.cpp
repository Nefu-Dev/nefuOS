// nefuOS File Search app
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../gui/widgets.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {

struct SearchState {
    int w, h;
    char query[64];
    int query_len;
};

static void search_paint(Window* win) {
    Surface& s = win->back;
    int W = win->content_w;
    int H = win->content_h;
    gfx::fillrect(s, 0, 0, W, H, 0x1e1e2e);

    SearchState* st = (SearchState*)win->userdata;

    gfx::text(s, 15, 15, "=== File Search ===", 0x89b4fa, 0);

    // Search box
    gfx::text(s, 15, 45, "Search:", 0xcdd6f4, 0);
    gfx::fillrect(s, 75, 35, W - 90, 22, 0x313244);
    gfx::rect(s, 75, 35, W - 90, 22, 0x4a9eff);
    gfx::text(s, 80, 40, st->query_len > 0 ? st->query : "(type to search)", 0xcdd6f4, 0);

    // Sample results
    int y = 80;
    gfx::text(s, 15, y, "Results:", 0xcdd6f4, 0);
    y += 25;

    const char* sample_files[] = {
        "Documents/readme.txt",
        "Pictures/wallpaper.png",
        "Music/song.mp3",
        "Downloads/setup.exe",
        "Videos/movie.mp4",
        "Desktop/shortcut.nefud",
    };

    for (int i = 0; i < 6 && y < H - 30; i++) {
        if (st->query_len > 0) {
            if (!strstr(sample_files[i], st->query)) continue;
        }
        gfx::text(s, 25, y, sample_files[i], 0xa6e3a1, 0);
        y += 22;
    }
}

static void search_on_key(Window* win, const KeyEvent* e) {
    SearchState* st = (SearchState*)win->userdata;
    if (!e->down) return;

    char key = e->ascii;
    if (key >= 32 && key < 127 && st->query_len < 63) {
        st->query[st->query_len++] = (char)key;
        st->query[st->query_len] = 0;
    } else if (key == 8) { // Backspace
        if (st->query_len > 0) {
            st->query[--st->query_len] = 0;
        }
    }
}

void app_search_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("File Search", x, y, 350, 280);
    if (!w) return;
    SearchState* st = new SearchState();
    st->w = w->content_w;
    st->h = w->content_h;
    st->query_len = 0;
    st->query[0] = 0;
    w->userdata = st;
    w->on_paint = search_paint;
    w->on_key = search_on_key;
    g_wm->raise(w);
}

} // namespace nefu
