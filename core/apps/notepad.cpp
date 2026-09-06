// nefuOS 记事本：多行文本编辑，保存到 /home/user/Documents/notes.txt
#include "apps.h"
#include "../gui/gfx.h"
#include "../platform.h"

namespace nefu {

struct NoteState {
    List<String> lines;
    int cur_row;       // 0..lines
    int cur_col;       // 0..len
    int view_scroll;
    String path;
    bool dirty;
    Window* win;
};

static void note_load(NoteState* st) {
    FSNode* f = g_vfs->resolve(st->path.c_str());
    if (f && !f->is_dir) {
        List<String> ls;
        file_to_lines(f, ls, 100000);
        st->lines.clear();
        for (int i = 0; i < ls.size(); i++) st->lines.push(ls[i]);
    }
    if (st->lines.empty()) st->lines.push(String()); // 始终保证至少一行
}

static void note_save(NoteState* st) {
    // 组装内容
    uint32_t total = 0;
    for (int i = 0; i < st->lines.size(); i++) total += (uint32_t)st->lines[i].len() + 1;
    uint8_t* buf = (uint8_t*)kalloc(total + 1);
    if (!buf) return;
    uint8_t* p = buf;
    for (int i = 0; i < st->lines.size(); i++) {
        memcpy(p, st->lines[i].c_str(), (size_t)st->lines[i].len());
        p += st->lines[i].len();
        *p++ = '\n';
    }
    FSNode* f = g_vfs->resolve(st->path.c_str());
    if (!f) {
        g_vfs->mkdir("/home/user/Documents");
        f = g_vfs->create_file(st->path.c_str());
    }
    if (f) {
        g_vfs->write_file(f, buf, (uint32_t)(p - buf));
        st->dirty = false;
    }
    kfree(buf);
}

static void note_paint(Window* w) {
    NoteState* st = (NoteState*)w->userdata;
    Surface& s = w->back;
    s.fill(color::WHITE);
    int vis = s.height / 16;
    if (st->cur_row < st->view_scroll) st->view_scroll = st->cur_row;
    if (st->cur_row >= st->view_scroll + vis) st->view_scroll = st->cur_row - vis + 1;
    if (st->view_scroll < 0) st->view_scroll = 0;
    for (int i = st->view_scroll; i < st->lines.size() && i < st->view_scroll + vis; i++) {
        gfx::text(s, 4, (i - st->view_scroll) * 16, st->lines[i].c_str(), color::TEXT, color::WHITE);
    }
    // 光标
    int cy = (st->cur_row - st->view_scroll) * 16;
    int cx = 4 + st->cur_col * 8;
    if ((platform_tick_ms() / 400) % 2 == 0 && cy >= 0 && cy < s.height) {
        gfx::fillrect(s, cx, cy, 2, 15, color::RED);
    }
    char status[48];
    ksprintf(status, sizeof(status), "%s  row %d col %d", st->dirty ? "*" : " ", st->cur_row + 1, st->cur_col);
    gfx::text(s, 4, s.height - 16, status, color::TEXT2, color::WHITE);
}

static void note_key(Window* w, const KeyEvent* e) {
    NoteState* st = (NoteState*)w->userdata;
    if (!e->down) return;
    if (e->keycode == KEY_CTRL && e->ascii == 0) { /* 占位 */ }
    if (e->ascii == 26) { note_save(st); return; } // Ctrl+Z 不用，避免误触发
    if (e->keycode == KEY_ENTER) {
        String rest = st->lines[st->cur_row].substr(st->cur_col, st->lines[st->cur_row].len() - st->cur_col);
        st->lines[st->cur_row] = st->lines[st->cur_row].substr(0, st->cur_col);
        st->cur_row++;
        st->lines.insert(st->cur_row, rest);
        st->cur_col = 0;
        st->dirty = true;
        return;
    }
    if (e->keycode == KEY_BACKSPACE) {
        if (st->cur_col > 0) {
            String& l = st->lines[st->cur_row];
            l = l.substr(0, st->cur_col - 1) + l.substr(st->cur_col, l.len() - st->cur_col);
            st->cur_col--;
            st->dirty = true;
        } else if (st->cur_row > 0) {
            int prev_len = st->lines[st->cur_row - 1].len();
            st->lines[st->cur_row - 1] += st->lines[st->cur_row];
            st->lines.remove(st->cur_row);
            st->cur_row--;
            st->cur_col = prev_len;
            st->dirty = true;
        }
        return;
    }
    if (e->keycode == KEY_LEFT) {
        if (st->cur_col > 0) st->cur_col--;
        else if (st->cur_row > 0) { st->cur_row--; st->cur_col = st->lines[st->cur_row].len(); }
        return;
    }
    if (e->keycode == KEY_RIGHT) {
        if (st->cur_col < st->lines[st->cur_row].len()) st->cur_col++;
        else if (st->cur_row + 1 < st->lines.size()) { st->cur_row++; st->cur_col = 0; }
        return;
    }
    if (e->keycode == KEY_UP && st->cur_row > 0) {
        st->cur_row--;
        if (st->cur_col > st->lines[st->cur_row].len()) st->cur_col = st->lines[st->cur_row].len();
        return;
    }
    if (e->keycode == KEY_DOWN && st->cur_row + 1 < st->lines.size()) {
        st->cur_row++;
        if (st->cur_col > st->lines[st->cur_row].len()) st->cur_col = st->lines[st->cur_row].len();
        return;
    }
    if (e->keycode == KEY_HOME) { st->cur_col = 0; return; }
    if (e->keycode == KEY_END) { st->cur_col = st->lines[st->cur_row].len(); return; }
    if (e->keycode == KEY_ESC) { note_save(st); return; } // Esc 保存
    if (e->ascii >= 32 && e->ascii < 127) {
        String& l = st->lines[st->cur_row];
        String ch;
        ch += e->ascii;
        l = l.substr(0, st->cur_col) + ch + l.substr(st->cur_col, l.len() - st->cur_col);
        st->cur_col++;
        st->dirty = true;
    }
}

static void note_scroll(Window* w, int delta) {
    NoteState* st = (NoteState*)w->userdata;
    st->view_scroll += delta > 0 ? -2 : 2;
    if (st->view_scroll < 0) st->view_scroll = 0;
}

static void note_close(Window* w) {
    NoteState* st = (NoteState*)w->userdata;
    if (st) {
        if (st->dirty) note_save(st);
        delete st;
        w->userdata = 0;
    }
}

void notepad_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Notepad", x, y, 560, 360);
    if (!w) return;
    NoteState* st = new NoteState();
    st->win = w;
    st->cur_row = 0;
    st->cur_col = 0;
    st->view_scroll = 0;
    st->path = "/home/user/Documents/notes.txt";
    st->dirty = false;
    note_load(st);
    w->userdata = st;
    w->on_paint = note_paint;
    w->on_key = note_key;
    w->on_scroll = note_scroll;
    w->on_close = note_close;
}

} // namespace nefu
