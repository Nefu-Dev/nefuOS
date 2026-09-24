// nefuOS Sticky Notes
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../vfs/vfs.h"
#include "../klib/klib.h"

namespace nefu {

struct StickyState {
    int w, h;
    String text;
    int cursor_pos;
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
    int cursor_line = -1;
    int cursor_col = -1;
    int current_line = 0;
    int current_col = 0;

    // Find cursor position
    for (int i = 0; i <= len; i++) {
        if (p[i] == '\n' || i == len) {
            if (st->cursor_pos >= line_start && st->cursor_pos <= i) {
                cursor_line = current_line;
                cursor_col = st->cursor_pos - line_start;
            }
            current_line++;
            line_start = i + 1;
            if (y > st->h - 40) break;
        }
    }

    // Draw text
    line_start = 0;
    current_line = 0;
    for (int i = 0; i <= len; i++) {
        if (p[i] == '\n' || i == len) {
            int line_len = i - line_start;
            if (line_len > cols) line_len = cols;
            char buf[128];
            int n = line_len < 127 ? line_len : 127;
            for (int k = 0; k < n; k++) buf[k] = p[line_start + k];
            buf[n] = 0;
            gfx::text(cs, 10, y, buf, 0x00333333, 0x00FFFACD);
            
            // Draw cursor on the right line
            if (current_line == cursor_line) {
                int cx = 10 + (cursor_col < line_len ? cursor_col : line_len) * 8;
                gfx::rect(cs, cx, y, 1, 14, 0x00333333);
            }
            
            y += 16;
            line_start = i + 1;
            current_line++;
            if (y > st->h - 40) break;
        }
    }

    // Bottom hint
    gfx::text(cs, 10, st->h - 25, "Type to edit, Backspace to delete", 0x00999966, 0x00FFFACD);
}

static void sticky_on_key(Window* win, const KeyEvent* e) {
    if (!e->down) return;
    StickyState* st = (StickyState*)win->userdata;
    if (!st) return;

    int keycode = e->keycode;
    char ascii = e->ascii;

    if (keycode == KEY_BACKSPACE) {
        // Delete character before cursor
        if (st->cursor_pos > 0) {
            char buf[256];
            const char* old = st->text.c_str();
            int len = st->text.len();
            for (int i = 0; i < st->cursor_pos - 1; i++) buf[i] = old[i];
            for (int i = st->cursor_pos; i < len; i++) buf[i - 1] = old[i];
            buf[len - 1] = 0;
            st->text = buf;
            st->cursor_pos--;
        }
    } else if (keycode == KEY_ENTER) {
        // Insert newline
        char buf[256];
        const char* old = st->text.c_str();
        int len = st->text.len();
        for (int i = 0; i < st->cursor_pos; i++) buf[i] = old[i];
        buf[st->cursor_pos] = '\n';
        for (int i = st->cursor_pos; i < len; i++) buf[i + 1] = old[i];
        buf[len + 1] = 0;
        st->text = buf;
        st->cursor_pos++;
    } else if (ascii >= 32 && ascii < 127) {
        // Insert printable character
        char buf[256];
        const char* old = st->text.c_str();
        int len = st->text.len();
        for (int i = 0; i < st->cursor_pos; i++) buf[i] = old[i];
        buf[st->cursor_pos] = ascii;
        for (int i = st->cursor_pos; i < len; i++) buf[i + 1] = old[i];
        buf[len + 1] = 0;
        st->text = buf;
        st->cursor_pos++;
    } else if (keycode == KEY_LEFT) {
        if (st->cursor_pos > 0) st->cursor_pos--;
    } else if (keycode == KEY_RIGHT) {
        if (st->cursor_pos < (int)st->text.len()) st->cursor_pos++;
    } else if (keycode == KEY_HOME) {
        st->cursor_pos = 0;
    } else if (keycode == KEY_END) {
        st->cursor_pos = st->text.len();
    }

    // Force repaint by calling paint directly
    sticky_paint(win);
}

void sticky_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Sticky Note", x, y, 220, 180);
    if (!w) return;
    StickyState* st = new StickyState();
    st->w = w->content_w;
    st->h = w->content_h;
    st->text = "Welcome to nefuOS!\n\nThis is a sticky note.\n\nStart typing to edit.";
    st->cursor_pos = st->text.len();
    w->userdata = st;
    w->on_paint = sticky_paint;
    w->on_key = sticky_on_key;
    g_wm->raise(w);
}

} // namespace nefu
