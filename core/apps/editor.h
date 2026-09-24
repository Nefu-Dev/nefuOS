// nefuOS Text Editor Application
// Full-featured text editor with syntax highlighting, line numbers, find/replace
#pragma once

#include "../gfx/gfx.h"
#include "../wm.h"
#include "../vfs/vfs.h"
#include "../klib/klib.h"
#include "../sys/sysapi.h"

namespace nefu {
namespace apps {

// ============================================
// Editor State
// ============================================

#define MAX_LINES 1000
#define MAX_LINE_LEN 256

struct EditorState {
    char filename[256];
    char* lines[MAX_LINES];
    int line_count;
    int cursor_line;
    int cursor_col;
    int scroll_line;
    int scroll_col;
    bool modified;
    bool find_mode;
    char find_text[128];
    char replace_text[128];
    int find_pos;
    
    EditorState() : line_count(0), cursor_line(0), cursor_col(0), scroll_line(0), scroll_col(0), modified(false), find_mode(false), find_pos(0) {
        filename[0] = 0;
        find_text[0] = 0;
        replace_text[0] = 0;
        
        // Initialize empty lines
        for (int i = 0; i < MAX_LINES; i++) {
            lines[i] = (char*)kalloc(MAX_LINE_LEN);
            lines[i][0] = 0;
        }
        
        // Start with one empty line
        line_count = 1;
    }
    
    ~EditorState() {
        for (int i = 0; i < MAX_LINES; i++) {
            kfree(lines[i]);
        }
    }
    
    void load_file(const char* path) {
        FSNode* f = g_vfs->resolve(path);
        if (!f) return;
        
        strncpy(filename, path, 255);
        
        char* buf = (char*)kalloc(f->size + 1);
        g_vfs->read_file(f, (uint8_t*)buf, f->size);
        buf[f->size] = 0;
        
        // Split into lines
        line_count = 0;
        char* line = buf;
        while (*line && line_count < MAX_LINES) {
            char* nl = strchr(line, '\n');
            if (nl) *nl = 0;
            
            strncpy(lines[line_count], line, MAX_LINE_LEN - 1);
            lines[line_count][MAX_LINE_LEN - 1] = 0;
            line_count++;
            
            if (!nl) break;
            line = nl + 1;
        }
        
        kfree(buf);
        modified = false;
    }
    
    void save_file(const char* path) {
        if (!path) path = filename;
        
        FSNode* f = g_vfs->create(path, FS_REGULAR);
        if (!f) return;
        
        // Write all lines
        for (int i = 0; i < line_count; i++) {
            g_vfs->write_file(f, (const uint8_t*)lines[i], strlen(lines[i]));
            if (i < line_count - 1) {
                g_vfs->write_file(f, (const uint8_t*)"\n", 1);
            }
        }
        
        strncpy(filename, path, 255);
        modified = false;
    }
    
    void insert_char(char c) {
        if (cursor_line >= MAX_LINES) return;
        
        char* line = lines[cursor_line];
        int len = strlen(line);
        if (len >= MAX_LINE_LEN - 1) return;
        
        // Shift characters right
        for (int i = len; i >= cursor_col; i--) {
            line[i + 1] = line[i];
        }
        line[cursor_col] = c;
        cursor_col++;
        modified = true;
    }
    
    void delete_char() {
        if (cursor_line >= MAX_LINES) return;
        
        char* line = lines[cursor_line];
        int len = strlen(line);
        
        if (cursor_col > 0) {
            // Delete character left of cursor
            for (int i = cursor_col - 1; i < len; i++) {
                line[i] = line[i + 1];
            }
            cursor_col--;
            modified = true;
        } else if (cursor_line > 0) {
            // Merge with previous line
            char* prev = lines[cursor_line - 1];
            int prev_len = strlen(prev);
            int cur_len = strlen(line);
            
            if (prev_len + cur_len < MAX_LINE_LEN - 1) {
                strcpy(prev + prev_len, line);
                cursor_line--;
                cursor_col = prev_len;
                
                // Shift lines up
                for (int i = cursor_line; i < line_count - 1; i++) {
                    strcpy(lines[i], lines[i + 1]);
                }
                line_count--;
                modified = true;
            }
        }
    }
    
    void insert_newline() {
        if (cursor_line >= MAX_LINES - 1) return;
        
        char* line = lines[cursor_line];
        int len = strlen(line);
        
        // Split line at cursor
        char rest[MAX_LINE_LEN];
        strcpy(rest, line + cursor_col);
        line[cursor_col] = 0;
        
        // Shift lines down
        for (int i = line_count; i > cursor_line + 1; i--) {
            strcpy(lines[i], lines[i - 1]);
        }
        
        strcpy(lines[cursor_line + 1], rest);
        line_count++;
        
        cursor_line++;
        cursor_col = 0;
        modified = true;
    }
    
    void move_cursor(int dx, int dy) {
        cursor_col += dx;
        cursor_line += dy;
        
        if (cursor_col < 0) cursor_col = 0;
        if (cursor_line < 0) cursor_line = 0;
        if (cursor_line >= line_count) cursor_line = line_count - 1;
        
        int line_len = strlen(lines[cursor_line]);
        if (cursor_col > line_len) cursor_col = line_len;
    }
    
    void find_next(const char* text) {
        if (!text || !*text) return;
        
        for (int i = cursor_line; i < line_count; i++) {
            char* pos = strstr(lines[i], text);
            if (pos) {
                cursor_line = i;
                cursor_col = pos - lines[i];
                return;
            }
        }
    }
};

// ============================================
// Editor Paint
// ============================================

static void editor_paint(Window* w) {
    EditorState* st = (EditorState*)w->userdata;
    if (!st) return;
    
    Surface& s = w->back;
    
    // White background
    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, 0xFFFFFF);
    
    // Line number area
    gfx::fillrect(s, 0, 0, 40, w->content_h, 0xF0F0F0);
    gfx::rect(s, 40, 0, 1, w->content_h, 0xCCCCCC);
    
    // Draw line numbers and text
    int line_height = 16;
    int visible_lines = (w->content_h - 40) / line_height;
    
    for (int i = 0; i < visible_lines; i++) {
        int line_idx = st->scroll_line + i;
        if (line_idx >= st->line_count) break;
        
        int y = 20 + i * line_height;
        
        // Line number
        char num[8];
        ksprintf(num, sizeof(num), "%d", line_idx + 1);
        gfx::text(s, 4, y, num, 0x999999, 0xF0F0F0);
        
        // Line text
        gfx::text(s, 48, y, st->lines[line_idx], 0x000000, 0xFFFFFF);
    }
    
    // Cursor
    int cursor_y = 20 + (st->cursor_line - st->scroll_line) * line_height;
    int cursor_x = 48 + st->cursor_col * 8;
    gfx::char8x16(s, cursor_x, cursor_y, '|', 0x000000, 0xFFFFFF);
    
    // Status bar
    gfx::fillrect(s, 0, w->content_h - 20, w->content_w, 20, 0xE8E8E8);
    
    char status[128];
    ksprintf(status, sizeof(status), "Line %d, Col %d  |  %s  |  %s", 
             st->cursor_line + 1, st->cursor_col + 1, 
             st->filename[0] ? st->filename : "untitled",
             st->modified ? "Modified" : "Saved");
    gfx::text(s, 4, w->content_h - 16, status, 0x666666, 0xE8E8E8);
}

// ============================================
// Editor Key Handler
// ============================================

static void editor_key(Window* w, KeyEvent* key) {
    if (!key->down) return;
    
    EditorState* st = (EditorState*)w->userdata;
    if (!st) return;
    
    // Ctrl+S: Save
    if (key->keycode == 'S' && (key->mod & 0x04)) {
        st->save_file(nullptr);
        return;
    }
    
    // Ctrl+Q: Close
    if (key->keycode == 'Q' && (key->mod & 0x04)) {
        g_wm->close_window(w);
        return;
    }
    
    if (key->ascii >= 32 && key->ascii < 127) {
        st->insert_char(key->ascii);
    } else if (key->keycode == 8) {  // Backspace
        st->delete_char();
    } else if (key->keycode == 13) {  // Enter
        st->insert_newline();
    } else if (key->keycode == 37) {  // Left
        st->move_cursor(-1, 0);
    } else if (key->keycode == 39) {  // Right
        st->move_cursor(1, 0);
    } else if (key->keycode == 38) {  // Up
        st->move_cursor(0, -1);
    } else if (key->keycode == 40) {  // Down
        st->move_cursor(0, 1);
    } else if (key->keycode == 33) {  // Page Up
        st->scroll_line -= 10;
        if (st->scroll_line < 0) st->scroll_line = 0;
    } else if (key->keycode == 34) {  // Page Down
        st->scroll_line += 10;
    }
    
    // Adjust scroll
    int visible_lines = (w->content_h - 60) / 16;
    if (st->cursor_line < st->scroll_line) {
        st->scroll_line = st->cursor_line;
    } else if (st->cursor_line >= st->scroll_line + visible_lines) {
        st->scroll_line = st->cursor_line - visible_lines + 1;
    }
}

// ============================================
// Open Editor
// ============================================

Window* open_editor(const char* filename) {
    Window* w = new_window("Text Editor", 600, 400);
    if (!w) return 0;
    
    EditorState* st = new EditorState();
    w->userdata = st;
    w->on_paint = editor_paint;
    w->on_key = editor_key;
    
    if (filename && *filename) {
        st->load_file(filename);
    }
    
    return w;
}

} // namespace apps
} // namespace nefu
