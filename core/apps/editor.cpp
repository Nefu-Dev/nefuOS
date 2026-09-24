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

    EditorState() : cursor_x(0), cursor_y(0), scroll_x(0), scroll_y(0), modified(false) {}
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

    // Code lines
    for (int i = st->scroll_y; i < lines.size() && (i - st->scroll_y) * line_h < w->content_h - content_y - 20; i++) {
        int y = content_y + (i - st->scroll_y) * line_h;
        uint32_t color = line_color(lines[i].c_str());
        gfx::text(s, 40 - st->scroll_x, y, lines[i].c_str(), color, 0xFFFFFF);
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

    switch (e->keycode) {
        case KEY_UP:
            if (st->cursor_y > 0) st->cursor_y--;
            break;
        case KEY_DOWN:
            if (st->cursor_y < lines.size() - 1) st->cursor_y++;
            break;
        case KEY_LEFT:
            if (st->cursor_x > 0) st->cursor_x--;
            break;
        case KEY_RIGHT:
            if (st->cursor_x < lines[st->cursor_y].len()) st->cursor_x++;
            break;
        case KEY_HOME:
            st->cursor_x = 0;
            break;
        case KEY_END:
            st->cursor_x = lines[st->cursor_y].len();
            break;
        case KEY_BACKSPACE:
            if (st->cursor_x > 0) {
                lines[st->cursor_y] = lines[st->cursor_y].substr(0, st->cursor_x - 1) +
                                      lines[st->cursor_y].substr(st->cursor_x, lines[st->cursor_y].len() - st->cursor_x);
                st->cursor_x--;
                st->modified = true;
            } else if (st->cursor_y > 0) {
                // Merge with previous line
                st->cursor_x = lines[st->cursor_y - 1].len();
                lines[st->cursor_y - 1] += lines[st->cursor_y];
                lines.remove(st->cursor_y);
                st->cursor_y--;
                st->modified = true;
            }
            st->content = join_lines(lines);
            break;
        case KEY_ENTER:
            // Split line
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
            // Insert spaces
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
                lines[st->cursor_y] = lines[st->cursor_y].substr(0, st->cursor_x) +
                                      ch +
                                      lines[st->cursor_y].substr(st->cursor_x, lines[st->cursor_y].len() - st->cursor_x);
                st->cursor_x++;
                st->content = join_lines(lines);
                st->modified = true;
            }
            break;
    }

    // Auto-scroll
    if (st->cursor_y < st->scroll_y) st->scroll_y = st->cursor_y;
    if (st->cursor_y > st->scroll_y + 20) st->scroll_y = st->cursor_y - 20;
}

static void editor_mouse(Window* w, int mx, int my, uint8_t buttons) {
    EditorState* st = (EditorState*)w->userdata;
    bool pressed = buttons;

    st->save_btn.id = 0;
    if (ui::button_event(st->save_btn, mx, my, buttons, pressed, false)) {
        // Save file
        if (!st->filename.empty()) {
            FSNode* f = g_vfs->resolve(st->filename.c_str());
            if (!f) f = g_vfs->create_file(st->filename.c_str());
            if (f) {
                g_vfs->write_file(f, (uint8_t*)st->content.c_str(), st->content.len());
                st->modified = false;
            }
        }
    }

    st->close_btn.id = 1;
    if (ui::button_event(st->close_btn, mx, my, buttons, pressed, false)) {
        // Close window
    }

    // Click to move cursor
    if (pressed && my > 35 && my < w->content_h - 20) {
        int line = (my - 35) / 16 + st->scroll_y;
        int col = (mx - 40 + st->scroll_x) / 8;
        if (line >= 0) st->cursor_y = line;
        if (col >= 0) st->cursor_x = col;
    }
}

static void editor_close(Window* w) {
    EditorState* st = (EditorState*)w->userdata;
    delete st;
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
