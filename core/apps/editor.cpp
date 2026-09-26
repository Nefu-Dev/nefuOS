// nefuOS Built-in Text Editor
// Simple code editor with syntax highlighting basics
// Based on neditor design (separate repo)

#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../gui/widgets.h"
#include "../klib/klib.h"
#include "../vfs/vfs.h"
#include <cstring>

namespace nefu {

namespace {

const int TAB_SIZE = 4;

struct EditorState {
    String filename;
    String content;
    int cursor_x, cursor_y;
    int scroll_x, scroll_y;
    Button save_btn;
    Button close_btn;
    bool modified;
    // selection (Shift+arrows) and single-level undo (Ctrl+Z)
    int sel_ax, sel_ay;
    bool has_sel;
    String undo_content;
    uint8_t last_buttons;   // previous mouse button state for press/release edges

    EditorState() : cursor_x(0), cursor_y(0), scroll_x(0), scroll_y(0), modified(false),
                    sel_ax(0), sel_ay(0), has_sel(false), last_buttons(0) {}
};

// Simple syntax highlighting (returns color for a line)
static uint32_t line_color(const char* line) {
    // Comments (start with // or #)
    if (strncmp(line, "//", 2) == 0 || strncmp(line, "#", 1) == 0) {
        return 0x008000;  // green = comment
    }
    // Keywords
    const char* keywords[] = { "int ", "void ", "char ", "if ", "else ", "for ", "while ", "return ", "class ", "struct ", "namespace ", nullptr };
    for (int i = 0; keywords[i]; i++) {
        if (strncmp(line, keywords[i], strlen(keywords[i])) == 0) {
            return 0x0000FF;  // blue = keyword
        }
    }
    // Strings (contain quotes)
    if (strchr(line, '"') || strchr(line, '\'')) {
        return 0x800080;  // purple = string
    }
    return 0x000000;  // black = normal
}

static List<String> split_lines(const String& content) {
    List<String> lines;
    const char* p = content.c_str();
    String cur;
    while (*p) {
        if (*p == '\n') {
            lines.push(cur);
            cur = "";
        } else {
            cur += *p;
        }
        p++;
    }
    lines.push(cur);
    return lines;
}

static String join_lines(const List<String>& lines) {
    String out;
    for (int i = 0; i < lines.size(); i++) {
        out += lines[i];
        if (i < lines.size() - 1) out += "\n";
    }
    return out;
}

// ---- selection / editing helpers (Shift+/Ctrl+ shortcuts) ----
// Move left/right by one character, jumping over UTF-8 continuation bytes so
// Chinese (IME) text is never split in the middle.
static int utf8_step_left(const String& s, int col) {
    if (col <= 0) return 0;
    col--;
    while (col > 0 && ((unsigned char)s[col] & 0xC0) == 0x80) col--;
    return col;
}
static int utf8_step_right(const String& s, int col) {
    int len = s.len();
    if (col >= len) return len;
    col++;
    while (col < len && ((unsigned char)s[col] & 0xC0) == 0x80) col++;
    return col;
}
// Normalize the selection bounds into (x1,y1)-(x2,y2) with (x1,y1) <= (x2,y2).
static void sel_norm(const EditorState* st, int& x1, int& y1, int& x2, int& y2) {
    if (!st->has_sel) { x1 = x2 = st->cursor_x; y1 = y2 = st->cursor_y; return; }
    if (st->sel_ay < st->cursor_y || (st->sel_ay == st->cursor_y && st->sel_ax <= st->cursor_x)) {
        x1 = st->sel_ax; y1 = st->sel_ay; x2 = st->cursor_x; y2 = st->cursor_y;
    } else {
        x1 = st->cursor_x; y1 = st->cursor_y; x2 = st->sel_ax; y2 = st->sel_ay;
    }
}
static String sel_text(const EditorState* st, const List<String>& lines) {
    int x1, y1, x2, y2;
    sel_norm(st, x1, y1, x2, y2);
    String out;
    if (y1 == y2) {
        out = lines[y1].substr(x1, x2 - x1);
    } else {
        out += lines[y1].substr(x1, lines[y1].len() - x1);
        for (int i = y1 + 1; i < y2; i++) { out += "\n"; out += lines[i]; }
        out += "\n";
        out += lines[y2].substr(0, x2);
    }
    return out;
}
static void editor_save(Window* w, EditorState* st) {
    (void)w;
    if (st->filename.empty()) return;
    FSNode* f = g_vfs->resolve(st->filename.c_str());
    if (!f) f = g_vfs->create_file(st->filename.c_str());
    if (f) {
        g_vfs->write_file(f, (uint8_t*)st->content.c_str(), st->content.len());
        st->modified = false;
    }
}
// Insert text (ASCII or UTF-8) at the cursor, replacing the selection when one
// is active. Saves a single-level undo snapshot first.
static void editor_insert(EditorState* st, const char* text, int byte_len) {
    if (byte_len <= 0 && !st->has_sel) return;
    st->undo_content = st->content;
    List<String> lines = split_lines(st->content);
    if (st->cursor_y < 0) st->cursor_y = 0;
    if (st->cursor_y >= lines.size()) st->cursor_y = lines.size() - 1;
    if (st->cursor_x < 0) st->cursor_x = 0;
    if (st->cursor_x > lines[st->cursor_y].len()) st->cursor_x = lines[st->cursor_y].len();
    int x1, y1, x2, y2;
    sel_norm(st, x1, y1, x2, y2);
    if (st->has_sel) {
        if (y1 == y2) {
            String l = lines[y1];
            lines[y1] = l.substr(0, x1) + l.substr(x2, l.len() - x2);
        } else {
            String l1 = lines[y1];
            String l2 = lines[y2];
            lines[y1] = l1.substr(0, x1) + l2.substr(x2, l2.len() - x2);
            for (int i = y2; i > y1; i--) lines.remove(i);
        }
        st->cursor_y = y1;
        st->cursor_x = x1;
        st->has_sel = false;
        st->content = join_lines(lines);
        lines = split_lines(st->content);
    }
    if (byte_len > 0) {
        String a = lines[st->cursor_y].substr(0, st->cursor_x);
        String b = lines[st->cursor_y].substr(st->cursor_x, lines[st->cursor_y].len() - st->cursor_x);
        lines[st->cursor_y] = a + String(text, byte_len) + b;
        st->cursor_x += byte_len;
    }
    st->content = join_lines(lines);
    st->modified = true;
}
static void editor_autoscroll(Window* w, EditorState* st) {
    (void)w;
    if (st->cursor_y < st->scroll_y) st->scroll_y = st->cursor_y;
    if (st->cursor_y > st->scroll_y + 20) st->scroll_y = st->cursor_y - 20;
    if (st->scroll_y < 0) st->scroll_y = 0;
    if (st->cursor_x < st->scroll_x) st->scroll_x = st->cursor_x;
    if (st->cursor_x > st->scroll_x + 50) st->scroll_x = st->cursor_x - 50;
    if (st->scroll_x < 0) st->scroll_x = 0;
}

static void editor_paint(Window* w) {
    EditorState* st = (EditorState*)w->userdata;
    Surface& s = w->back;

    // Background
    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, 0xFFFFFF);

    // Top bar
    gfx::fillrect(s, 0, 0, w->content_w, 30, 0xF0F0F0);

    // Filename
    String title = st->filename.empty() ? "untitled" : st->filename;
    if (st->modified) title += " *";
    gfx::text(s, 8, 8, title.c_str(), 0x333333, 0xF0F0F0);

    // Buttons
    st->save_btn.x = w->content_w - 140;
    st->save_btn.y = 3;
    st->save_btn.w = 60;
    st->save_btn.h = 24;
    st->save_btn.label = "Save";
    ui::draw_button(s, st->save_btn);

    st->close_btn.x = w->content_w - 70;
    st->close_btn.y = 3;
    st->close_btn.w = 60;
    st->close_btn.h = 24;
    st->close_btn.label = "Close";
    ui::draw_button(s, st->close_btn);

    // Content area
    int content_y = 35;
    int line_h = 16;
    List<String> lines = split_lines(st->content);

    // Line numbers
    for (int i = st->scroll_y; i < lines.size() && (i - st->scroll_y) * line_h < w->content_h - content_y - 20; i++) {
        int y = content_y + (i - st->scroll_y) * line_h;
        char num[8];
        ksprintf(num, sizeof(num), "%d", i + 1);
        gfx::text(s, 4, y, num, 0x999999, 0xFFFFFF);
    }

    // Code lines (with Shift-selection highlight)
    int sx1 = 0, sy1 = 0, sx2 = 0, sy2 = 0;
    if (st->has_sel) sel_norm(st, sx1, sy1, sx2, sy2);
    for (int i = st->scroll_y; i < lines.size() && (i - st->scroll_y) * line_h < w->content_h - content_y - 20; i++) {
        int y = content_y + (i - st->scroll_y) * line_h;
        uint32_t color = line_color(lines[i].c_str());
        int lx = 40 - st->scroll_x;
        if (st->has_sel && i >= sy1 && i <= sy2) {
            int c1 = (i == sy1) ? sx1 : 0;
            int c2 = (i == sy2) ? sx2 : lines[i].len();
            if (c1 < 0) c1 = 0;
            if (c2 > lines[i].len()) c2 = lines[i].len();
            if (c1 > c2) c1 = c2;
            gfx::text(s, lx, y, lines[i].substr(0, c1).c_str(), color, 0xFFFFFF);
            gfx::fillrect(s, lx + c1 * 8, y, (c2 - c1) * 8, line_h, 0xC9E6FF);
            gfx::text(s, lx + c1 * 8, y, lines[i].substr(c1, c2 - c1).c_str(), 0x000000, 0xC9E6FF);
            gfx::text(s, lx + c2 * 8, y, lines[i].substr(c2, lines[i].len() - c2).c_str(), color, 0xFFFFFF);
        } else {
            gfx::text(s, lx, y, lines[i].c_str(), color, 0xFFFFFF);
        }
    }

    // Cursor
    int cursor_y = content_y + (st->cursor_y - st->scroll_y) * line_h;
    int cursor_x = 40 + st->cursor_x * 8 - st->scroll_x;
    gfx::fillrect(s, cursor_x, cursor_y, 1, line_h - 2, 0x000000);

    // Status bar
    gfx::fillrect(s, 0, w->content_h - 20, w->content_w, 20, 0xE0E0E0);
    char status[64];
    ksprintf(status, sizeof(status), "Ln %d, Col %d | %d lines | %d bytes",
             st->cursor_y + 1, st->cursor_x + 1, lines.size(), st->content.len());
    gfx::text(s, 4, w->content_h - 16, status, 0x666666, 0xE0E0E0);
}

static void editor_key(Window* w, const KeyEvent* e) {
    EditorState* st = (EditorState*)w->userdata;
    if (!e->down) return;

    List<String> lines = split_lines(st->content);
    const bool ctrl  = (e->mods & 0x02) != 0;
    const bool shift = (e->mods & 0x01) != 0;

    // ---- UTF-8 (IME) committed text: insert at cursor, replacing selection ----
    if (e->utf8[0]) {
        editor_insert(st, e->utf8, (int)strlen(e->utf8));
        editor_autoscroll(w, st);
        return;
    }
    const char c = e->ascii;
    const bool is_c = (c == 'c' || c == 'C');
    const bool is_v = (c == 'v' || c == 'V');
    const bool is_x = (c == 'x' || c == 'X');
    const bool is_a = (c == 'a' || c == 'A');
    const bool is_s = (c == 's' || c == 'S');
    const bool is_q = (c == 'q' || c == 'Q');
    const bool is_z = (c == 'z' || c == 'Z');

    // ---- Ctrl shortcuts ----
    if (ctrl) {
        if (is_z) {  // Ctrl+Z: undo (single-level)
            if (st->undo_content.len() > 0) {
                st->content = st->undo_content;
                st->undo_content.clear();
                st->modified = true;
                List<String> nl = split_lines(st->content);
                if (st->cursor_y >= nl.size()) st->cursor_y = nl.size() - 1;
                if (st->cursor_y < 0) st->cursor_y = 0;
                if (st->cursor_x > nl[st->cursor_y].len()) st->cursor_x = nl[st->cursor_y].len();
                st->has_sel = false;
            }
            editor_autoscroll(w, st);
            return;
        }
        if (is_s) {  // Ctrl+S: save
            editor_save(w, st);
            return;
        }
        if (is_q) {  // Ctrl+Q: close
            g_wm->close_window(w);
            return;
        }
        if (is_a) {  // Ctrl+A: select all
            int last = lines.size() - 1;
            st->sel_ax = 0;
            st->sel_ay = 0;
            st->cursor_x = lines[last].len();
            st->cursor_y = last;
            st->has_sel = true;
            editor_autoscroll(w, st);
            return;
        }
        if (is_c || is_x) {  // Ctrl+C copy / Ctrl+X cut
            if (st->has_sel) {
                String t = sel_text(st, lines);
                clipboard_add(t.c_str());
                if (is_x) {
                    st->undo_content = st->content;
                    int x1, y1, x2, y2;
                    sel_norm(st, x1, y1, x2, y2);
                    if (y1 == y2) {
                        String l = lines[y1];
                        lines[y1] = l.substr(0, x1) + l.substr(x2, l.len() - x2);
                    } else {
                        String l1 = lines[y1];
                        String l2 = lines[y2];
                        lines[y1] = l1.substr(0, x1) + l2.substr(x2, l2.len() - x2);
                        for (int i = y2; i > y1; i--) lines.remove(i);
                    }
                    st->cursor_y = y1;
                    st->cursor_x = x1;
                    st->has_sel = false;
                    st->content = join_lines(lines);
                    st->modified = true;
                    editor_autoscroll(w, st);
                }
            }
            return;
        }
        if (is_v) {  // Ctrl+V: paste
            const char* cb = clipboard_get(0);
            if (cb && *cb) {
                editor_insert(st, cb, (int)strlen(cb));
                editor_autoscroll(w, st);
            }
            return;
        }
        return;  // other Ctrl combos are not editor shortcuts
    }

    // ---- Shift+/plain navigation ----
    switch (e->keycode) {
        case KEY_UP:
            if (shift && !st->has_sel) { st->sel_ax = st->cursor_x; st->sel_ay = st->cursor_y; st->has_sel = true; }
            if (st->cursor_y > 0) st->cursor_y--;
            if (!shift) st->has_sel = false;
            break;
        case KEY_DOWN:
            if (shift && !st->has_sel) { st->sel_ax = st->cursor_x; st->sel_ay = st->cursor_y; st->has_sel = true; }
            if (st->cursor_y < lines.size() - 1) st->cursor_y++;
            if (!shift) st->has_sel = false;
            break;
        case KEY_LEFT:
            if (shift && !st->has_sel) { st->sel_ax = st->cursor_x; st->sel_ay = st->cursor_y; st->has_sel = true; }
            st->cursor_x = utf8_step_left(lines[st->cursor_y], st->cursor_x);
            if (!shift) st->has_sel = false;
            break;
        case KEY_RIGHT:
            if (shift && !st->has_sel) { st->sel_ax = st->cursor_x; st->sel_ay = st->cursor_y; st->has_sel = true; }
            st->cursor_x = utf8_step_right(lines[st->cursor_y], st->cursor_x);
            if (!shift) st->has_sel = false;
            break;
        case KEY_HOME:
            if (shift && !st->has_sel) { st->sel_ax = st->cursor_x; st->sel_ay = st->cursor_y; st->has_sel = true; }
            st->cursor_x = 0;
            if (!shift) st->has_sel = false;
            break;
        case KEY_END:
            if (shift && !st->has_sel) { st->sel_ax = st->cursor_x; st->sel_ay = st->cursor_y; st->has_sel = true; }
            st->cursor_x = lines[st->cursor_y].len();
            if (!shift) st->has_sel = false;
            break;
        case KEY_BACKSPACE:
            if (st->has_sel) {
                editor_insert(st, "", 0);
            } else if (st->cursor_x > 0) {
                st->undo_content = st->content;
                int nl = utf8_step_left(lines[st->cursor_y], st->cursor_x);
                lines[st->cursor_y] = lines[st->cursor_y].substr(0, nl) +
                                      lines[st->cursor_y].substr(st->cursor_x, lines[st->cursor_y].len() - st->cursor_x);
                st->cursor_x = nl;
                st->content = join_lines(lines);
                st->modified = true;
            } else if (st->cursor_y > 0) {
                st->undo_content = st->content;
                st->cursor_x = lines[st->cursor_y - 1].len();
                lines[st->cursor_y - 1] += lines[st->cursor_y];
                lines.remove(st->cursor_y);
                st->cursor_y--;
                st->content = join_lines(lines);
                st->modified = true;
            }
            break;
        case KEY_ENTER:
            if (st->has_sel) editor_insert(st, "", 0);
            lines = split_lines(st->content);
            st->undo_content = st->content;
            {
                String left = lines[st->cursor_y].substr(0, st->cursor_x);
                String right = lines[st->cursor_y].substr(st->cursor_x, lines[st->cursor_y].len() - st->cursor_x);
                lines[st->cursor_y] = left;
                lines.insert(st->cursor_y + 1, right);
                st->cursor_y++;
                st->cursor_x = 0;
                st->content = join_lines(lines);
                st->modified = true;
            }
            break;
        case KEY_TAB:
            if (st->has_sel) editor_insert(st, "", 0);
            lines = split_lines(st->content);
            st->undo_content = st->content;
            for (int i = 0; i < TAB_SIZE; i++) {
                lines[st->cursor_y] = lines[st->cursor_y].substr(0, st->cursor_x) +
                                      " " +
                                      lines[st->cursor_y].substr(st->cursor_x, lines[st->cursor_y].len() - st->cursor_x);
                st->cursor_x++;
            }
            st->content = join_lines(lines);
            st->modified = true;
            break;
        default:
            if (e->ascii >= 32 && e->ascii < 127) {
                char ch[2] = { e->ascii, 0 };
                editor_insert(st, ch, 1);
            }
            break;
    }

    editor_autoscroll(w, st);
}

// Button callbacks: fired by ui::button_event on a completed click
// (press inside, release inside). The return value of button_event is a
// hover flag and must NOT be used to trigger actions.
static void editor_on_save_click(void* ud) {
    Window* w = (Window*)ud;
    EditorState* st = (EditorState*)w->userdata;
    editor_save(w, st);
}

static void editor_on_close_click(void* ud) {
    Window* w = (Window*)ud;
    g_wm->close_window(w);
}

static void editor_mouse(Window* w, int mx, int my, uint8_t buttons) {
    EditorState* st = (EditorState*)w->userdata;
    // Press/release edges from the raw button mask. Previously the code passed
    // `pressed = buttons` (level) and `released = false`, so ui::button_event
    // never completed a click: moving the mouse over "Close" closed the window
    // instantly and "Save" saved on mere hover. Track last_buttons and hand
    // real edges to the widget code.
    bool pressed = buttons && !st->last_buttons;
    bool released = !buttons && st->last_buttons;
    st->last_buttons = buttons;

    st->save_btn.id = 0;
    ui::button_event(st->save_btn, mx, my, buttons, pressed, released);
    st->close_btn.id = 1;
    ui::button_event(st->close_btn, mx, my, buttons, pressed, released);

    // Click to move cursor (clears any Shift selection). Clamp to the actual
    // text bounds: clicking far below the last line used to set cursor_y past
    // the line list, and the next keystroke read/wrote out of bounds -> crash.
    if (pressed && my > 35 && my < w->content_h - 20) {
        List<String> lines = split_lines(st->content);
        int line = (my - 35) / 16 + st->scroll_y;
        int col = (mx - 40 + st->scroll_x) / 8;
        if (line < 0) line = 0;
        if (line >= lines.size()) line = lines.size() - 1;
        if (col < 0) col = 0;
        if (col > lines[line].len()) col = lines[line].len();
        st->cursor_y = line;
        st->cursor_x = col;
        st->has_sel = false;
    }
}

static void editor_close(Window* w) {
    EditorState* st = (EditorState*)w->userdata;
    delete st;
    w->userdata = 0;
}

} // namespace

void editor_launch_file(const char* path) {
    EditorState* st = new EditorState();

    if (path && *path) {
        st->filename = path;
        FSNode* f = g_vfs->resolve(path);
        if (f && !f->is_dir) {
            // Read file content into String
            st->content = "";
            for (uint32_t bi = 0; bi < f->size; bi++) {
                st->content += (char)f->data[bi];
            }
        }
    }

    Window* w = g_wm->create_window("Text Editor", 80, 60, 600, 450);
    w->userdata = st;
    w->on_paint = editor_paint;
    w->on_key = editor_key;
    w->on_mouse = editor_mouse;
    w->on_close = editor_close;
    st->save_btn.on_click = editor_on_save_click;
    st->save_btn.ud = w;
    st->close_btn.on_click = editor_on_close_click;
    st->close_btn.ud = w;
    g_wm->raise(w);
}

void editor_launch() {
    editor_launch_file("/home/user/untitled.txt");
}

// App launcher: called from file manager when opening a code/text file
void app_show_editor(FSNode* file) {
    if (!file) {
        editor_launch();
        return;
    }
    String path = node_path(file);
    editor_launch_file(path.c_str());
}

} // namespace nefu
