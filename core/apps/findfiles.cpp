// nefuOS File Search — recursive name/pattern search with results list
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../gui/widgets.h"
#include "../platform.h"
#include "../vfs/vfs.h"

namespace nefu {

namespace {

struct FindFiles {
    int w, h;
    String pattern;
    String results[128];
    int count;
    int scroll;
    int sel;
    bool searching;
    bool case_sensitive;
    Button btns[3];
    uint8_t last_buttons;
};

bool ff_glob(const char* name, const char* pat, bool cs);

void ff_walk(FindFiles& g, FSNode* dir, const char* path) {
    if (g.count >= 128) return;
    for (int i = 0; i < dir->children.size() && g.count < 128; i++) {
        FSNode* c = dir->children[i];
        String full = path;
        full += '/';
        full += c->name;
        // match pattern (simple substring or '*')
        bool match = false;
        const char* pat = g.pattern.c_str();
        int plen = g.pattern.len();
        if (plen == 0) match = true;
        else if (plen == 1 && pat[0] == '*') match = true;
        else {
            // wildcard match
            const char* n = c->name.c_str();
            const char* p = pat;
            match = ff_glob(n, p, g.case_sensitive);
        }
        if (match) {
            g.results[g.count++] = full;
        }
        if (c->is_dir) ff_walk(g, c, full.c_str());
    }
}

bool ff_glob(const char* name, const char* pat, bool cs) {
    if (!*pat) return !*name;
    if (*pat == '*') {
        for (const char* q = name; ; q++) {
            if (ff_glob(q, pat + 1, cs)) return true;
            if (!*q) break;
        }
        return false;
    }
    if (!*name) return false;
    char a = *name, b = *pat;
    if (!cs) {
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
    }
    if (*pat == '?') return ff_glob(name + 1, pat + 1, cs);
    if (a != b) return false;
    return ff_glob(name + 1, pat + 1, cs);
}

void ff_run_search(FindFiles& g) {
    g.count = 0;
    g.scroll = 0;
    g.sel = 0;
    g.searching = true;
    ff_walk(g, g_vfs->root(), "");
    g.searching = false;
}

void ff_paint(Window* win) {
    FindFiles* g = (FindFiles*)win->userdata;
    Surface& s = win->back;
    s.fill(0x00F5F5F0);
    gfx::text(s, 8, 6, "File Search", color::TEXT, 0x00F5F5F0);
    // pattern box
    gfx::fillrect(s, 8, 28, g->w - 16, 24, 0x00FFFFFF);
    gfx::rect(s, 8, 28, g->w - 16, 24, color::BORDER);
    gfx::text(s, 12, 34, g->pattern.c_str(), 0x002266CC, 0x00FFFFFF);
    if (g->pattern.empty())
        gfx::text(s, 12, 34, "enter pattern (e.g. *.cpp, *conf*)", 0x00999999, 0x00FFFFFF);
    // buttons
    g->btns[0].x = 8; g->btns[0].y = 58; g->btns[0].w = 90; g->btns[0].h = 26;
    g->btns[0].label = "Search"; g->btns[0].id = 0;
    g->btns[1].x = 104; g->btns[1].y = 58; g->btns[1].w = 120; g->btns[1].h = 26;
    g->btns[1].label = "Case: off"; g->btns[1].id = 1;
    ui::draw_button(s, g->btns[0]);
    ui::draw_button(s, g->btns[1]);
    char buf[64];
    ksprintf(buf, sizeof(buf), "%d results", g->count);
    gfx::text(s, 8, 92, buf, 0x00888888, 0x00F5F5F0);
    // results list
    int y = 110;
    for (int i = g->scroll; i < g->count && y < g->h - 30; i++) {
        uint32_t bg = (i == g->sel) ? 0x00D8E8F8 : 0x00F5F5F0;
        gfx::fillrect(s, 8, y, g->w - 16, 18, bg);
        gfx::text(s, 12, y + 1, g->results[i].c_str(), 0x00224466, bg);
        y += 20;
    }
    gfx::text(s, 8, g->h - 18, "Enter: edit in editor   R: clear   U/D: navigate", 0x00888888, 0x00F5F5F0);
}

void ff_mouse(Window* w, int mx, int my, uint8_t buttons) {
    FindFiles* g = (FindFiles*)w->userdata;
    bool pressed = buttons && !g->last_buttons;
    bool released = !buttons && g->last_buttons;
    for (int i = 0; i < 2; i++) {
        if (ui::button_event(g->btns[i], mx, my, buttons, pressed, released)) {
            if (g->btns[i].id == 0) ff_run_search(*g);
            else {
                g->case_sensitive = !g->case_sensitive;
                g->btns[1].label = g->case_sensitive ? "Case: on" : "Case: off";
            }
        }
    }
    // click result
    if (buttons && !g->last_buttons) {
        int y = 110;
        int idx = g->scroll;
        while (y < g->h - 30 && idx < g->count) {
            if (my >= y && my < y + 20) { g->sel = idx; break; }
            y += 20;
            idx++;
        }
    }
    g->last_buttons = buttons;
}

void ff_key(Window* w, const KeyEvent* e) {
    FindFiles* g = (FindFiles*)w->userdata;
    if (!e->down) return;
    if (e->ascii >= 32 && e->ascii < 127) {
        g->pattern += e->ascii;
        return;
    }
    if (e->keycode == KEY_BACKSPACE && !g->pattern.empty()) {
        String s = g->pattern.substr(0, g->pattern.len() - 1);
        g->pattern = s;
        return;
    }
    if (e->keycode == KEY_ENTER) {
        if (g->count > 0 && g->sel < g->count) {
            FSNode* f = g_vfs->resolve(g->results[g->sel].c_str());
            if (f && !f->is_dir) app_show_editor(f);
        }
        return;
    }
    switch (e->keycode) {
    case KEY_DOWN: if (g->sel < g->count - 1) g->sel++;
        break;
    case KEY_UP: if (g->sel > 0) g->sel--;
        break;
    case KEY_PGDN: g->sel += 10; if (g->sel >= g->count) g->sel = g->count - 1;
        break;
    case KEY_PGUP: g->sel -= 10; if (g->sel < 0) g->sel = 0;
        break;
    default: break;
    }
    if (g->sel >= g->scroll + 6) g->scroll = g->sel - 5;
    if (g->sel < g->scroll) g->scroll = g->sel;
}

void ff_close(Window* w) {
    if (w->userdata) delete (FindFiles*)w->userdata;
    w->userdata = 0;
}

} // namespace

void findfiles_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("File Search", x, y, 420, 420);
    if (!w) return;
    FindFiles* g = new FindFiles();
    g->w = w->content_w;
    g->h = w->content_h;
    g->count = 0;
    g->scroll = 0;
    g->sel = 0;
    g->searching = false;
    g->case_sensitive = false;
    g->last_buttons = 0;
    w->userdata = g;
    w->on_paint = ff_paint;
    w->on_mouse = ff_mouse;
    w->on_key = ff_key;
    w->on_close = ff_close;
    g_wm->raise(w);
}

} // namespace nefu
