// nefuOS Word Count — count lines, words, chars, bytes in a file
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../gui/widgets.h"
#include "../platform.h"
#include "../vfs/vfs.h"

namespace nefu {

namespace {

struct WordCount {
    int w, h;
    String path;
    int lines, words, chars, bytes;
    Button btns[2];
    uint8_t last_buttons;
};

bool wc_analyze(WordCount& g, const char* path) {
    FSNode* f = g_vfs->resolve(path);
    if (!f || f->is_dir) return false;
    g.path = path;
    uint8_t* d = (uint8_t*)kalloc(f->size + 1);
    if (!d) return false;
    memcpy(d, f->data, f->size);
    d[f->size] = 0;
    g.bytes = (int)f->size;
    g.lines = 0;
    g.words = 0;
    g.chars = 0;
    bool in_word = false;
    for (uint32_t i = 0; i < f->size; i++) {
        char c = (char)d[i];
        if (c == '\n') g.lines++;
        g.chars++;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            in_word = false;
        } else {
            if (!in_word) { g.words++; in_word = true; }
        }
    }
    if (f->size > 0) g.lines++;
    kfree(d);
    return true;
}

void wc_paint(Window* win) {
    WordCount* g = (WordCount*)win->userdata;
    Surface& s = win->back;
    s.fill(0x00F5F5F0);
    gfx::text_scale(s, 10, 8, "Word Count", color::TEXT, 0x00F5F5F0, 2);
    gfx::text(s, 10, 44, g->path.c_str(), 0x002266CC, 0x00F5F5F0);
    char buf[64];
    int y = 80;
    ksprintf(buf, sizeof(buf), "Lines : %d", g->lines);
    gfx::text(s, 16, y, buf, color::TEXT, 0x00F5F5F0); y += 24;
    ksprintf(buf, sizeof(buf), "Words : %d", g->words);
    gfx::text(s, 16, y, buf, color::TEXT, 0x00F5F5F0); y += 24;
    ksprintf(buf, sizeof(buf), "Chars : %d", g->chars);
    gfx::text(s, 16, y, buf, color::TEXT, 0x00F5F5F0); y += 24;
    ksprintf(buf, sizeof(buf), "Bytes : %d", g->bytes);
    gfx::text(s, 16, y, buf, color::TEXT, 0x00F5F5F0); y += 40;
    // buttons: change file, recount
    g->btns[0].x = 10; g->btns[0].y = 200; g->btns[0].w = (g->w - 30) / 2; g->btns[0].h = 28;
    g->btns[0].label = "Next File"; g->btns[0].id = 0;
    g->btns[1].x = 20 + g->btns[0].w; g->btns[1].y = 200; g->btns[1].w = g->btns[0].w; g->btns[1].h = 28;
    g->btns[1].label = "Recount"; g->btns[1].id = 1;
    ui::draw_button(s, g->btns[0]);
    ui::draw_button(s, g->btns[1]);
    gfx::text(s, 10, g->h - 16, "Type a path and press Enter to analyze", 0x00888888, 0x00F5F5F0);
}

void wc_mouse(Window* w, int mx, int my, uint8_t buttons) {
    WordCount* g = (WordCount*)w->userdata;
    bool pressed = buttons && !g->last_buttons;
    bool released = !buttons && g->last_buttons;
    for (int i = 0; i < 2; i++) {
        if (ui::button_event(g->btns[i], mx, my, buttons, pressed, released)) {
            if (g->btns[i].id == 1) {
                if (!g->path.empty()) wc_analyze(*g, g->path.c_str());
            }
            // id 0 = cycle to next file (simple)
        }
    }
    g->last_buttons = buttons;
}

void wc_key(Window* w, const KeyEvent* e) {
    WordCount* g = (WordCount*)w->userdata;
    if (!e->down) return;
    if (e->ascii >= 32 && e->ascii < 127) {
        g->path += e->ascii;
        return;
    }
    if (e->keycode == KEY_BACKSPACE && !g->path.empty()) {
        g->path = g->path.substr(0, g->path.len() - 1);
        return;
    }
    if (e->keycode == KEY_ENTER) {
        wc_analyze(*g, g->path.c_str());
    }
}

void wc_close(Window* w) {
    if (w->userdata) delete (WordCount*)w->userdata;
    w->userdata = 0;
}

} // namespace

void wordcount_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Word Count", x, y, 320, 260);
    if (!w) return;
    WordCount* g = new WordCount();
    g->w = w->content_w;
    g->h = w->content_h;
    g->lines = g->words = g->chars = g->bytes = 0;
    g->last_buttons = 0;
    g->path = "/home/user/Documents/nefuos.txt";
    wc_analyze(*g, g->path.c_str());
    w->userdata = g;
    w->on_paint = wc_paint;
    w->on_mouse = wc_mouse;
    w->on_key = wc_key;
    w->on_close = wc_close;
    g_wm->raise(w);
}

} // namespace nefu
