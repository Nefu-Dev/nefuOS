// nefuOS Hex Editor — view and edit raw bytes of VFS files
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../gui/widgets.h"
#include "../platform.h"
#include "../vfs/vfs.h"

namespace nefu {

namespace {

struct HexEdit {
    int w, h;
    String path;
    uint8_t* data;
    uint32_t size;
    uint32_t cap;
    int offset;         // top row offset
    int sel;            // selected byte index
    int rows, cols;     // layout
    bool dirty;
    bool view_ascii;
};

const int HEX_ROWS_VIS = 20;
const int HEX_BYTES = 16;

bool hex_load(HexEdit& g, const char* path) {
    FSNode* f = g_vfs->resolve(path);
    if (!f || f->is_dir) return false;
    g.path = path;
    g.size = f->size;
    g.cap = f->size + 4096;
    g.data = (uint8_t*)kalloc(g.cap);
    if (!g.data) return false;
    memcpy(g.data, f->data, f->size);
    g.offset = 0;
    g.sel = 0;
    g.dirty = false;
    return true;
}

void hex_save(HexEdit& g) {
    FSNode* f = g_vfs->resolve(g.path.c_str());
    if (!f) f = g_vfs->create_file(g.path.c_str());
    if (f) {
        g_vfs->write_file(f, g.data, g.size);
        g.dirty = false;
    }
}

void hex_paint(Window* win) {
    HexEdit* g = (HexEdit*)win->userdata;
    Surface& s = win->back;
    s.fill(0x00121A24);
    char buf[128];
    ksprintf(buf, sizeof(buf), "Hex Editor  %s  [%u bytes]", g->path.c_str(), (unsigned)g->size);
    gfx::text(s, 6, 4, buf, color::TEXT, 0x00121A24);
    // header
    gfx::text(s, 10, 22, "offset", 0x0088AA88, 0x00121A24);
    gfx::text(s, 90, 22, "00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F", 0x0088AA88, 0x00121A24);
    gfx::text(s, 300, 22, "ascii", 0x0088AA88, 0x00121A24);
    gfx::hline(s, 8, g->w - 8, 33, 0x00304050);
    int y = 40;
    for (int r = 0; r < HEX_ROWS_VIS; r++) {
        int base = g->offset + r * HEX_BYTES;
        if (base >= (int)g->size && r > 0) break;
        char line[96];
        ksprintf(line, sizeof(line), "%08x", (unsigned)(base < 0 ? 0 : base));
        gfx::text(s, 10, y, line, 0x0088AA88, 0x00121A24);
        // bytes
        for (int c = 0; c < HEX_BYTES; c++) {
            int idx = base + c;
            int x = 90 + c * 14;
            if (idx < (int)g->size) {
                uint32_t bg = 0x00121A24;
                if (idx == g->sel) bg = 0x00224488;
                ksprintf(line, sizeof(line), "%02x", g->data[idx]);
                gfx::text(s, x, y, line, 0x00D6E2D8, bg);
            }
        }
        // ascii column
        for (int c = 0; c < HEX_BYTES; c++) {
            int idx = base + c;
            if (idx < (int)g->size) {
                char cc = (char)g->data[idx];
                char out[2] = {(cc >= 32 && cc < 127) ? cc : '.', 0};
                uint32_t bg = 0x00121A24;
                if (idx == g->sel) bg = 0x00224488;
                gfx::text(s, 300 + c * 8, y, out, 0x00AACCDD, bg);
            }
        }
        y += 15;
    }
    gfx::hline(s, 8, g->w - 8, y + 2, 0x00304050);
    ksprintf(buf, sizeof(buf), "Arrows/WASD move  Enter=edit byte  S=save  F=ascii  +/-: grow  Del: remove");
    gfx::text(s, 6, y + 10, buf, 0x00888888, 0x00121A24);
    if (g->dirty) gfx::text(s, 6, y + 26, "* modified", 0x00E0A040, 0x00121A24);
}

// edit current byte
void hex_edit_byte(HexEdit& g, int newval) {
    if (g.sel < (int)g.size) {
        g.data[g.sel] = (uint8_t)(newval & 0xFF);
        g.dirty = true;
    }
}

void hex_key(Window* w, const KeyEvent* e) {
    HexEdit* g = (HexEdit*)w->userdata;
    if (!e->down) return;
    switch (e->ascii) {
    case 's': case 'S': hex_save(*g); return;
    case 'f': case 'F': g->view_ascii = !g->view_ascii; return;
    case '=': case '+':
        if (g->size < g->cap - 1) { g->data[g->size++] = 0; g->dirty = true; }
        return;
    case '-': case '_':
        if (g->size > 0) { g->size--; g->dirty = true; }
        return;
    default: break;
    }
    // hex digit editing: hold a nibble mode is complex; use simple:
    // digits 0-9 a-f directly overwrite the byte (nibble editing via tab toggle)
    if ((e->ascii >= '0' && e->ascii <= '9')) {
        hex_edit_byte(*g, (e->ascii - '0') | (g->data[g->sel] & 0xF0));
        g->sel = (g->sel + 1) % (int)g->size;
        return;
    }
    if ((e->ascii >= 'a' && e->ascii <= 'f')) {
        hex_edit_byte(*g, (e->ascii - 'a' + 10) | (g->data[g->sel] & 0xF0));
        g->sel = (g->sel + 1) % (int)g->size;
        return;
    }
    switch (e->keycode) {
    case KEY_LEFT:
        if (g->sel > 0) g->sel--;
        break;
    case KEY_RIGHT:
        if (g->sel < (int)g->size - 1) g->sel++;
        break;
    case KEY_UP:
        g->sel -= HEX_BYTES;
        if (g->sel < 0) g->sel = 0;
        break;
    case KEY_DOWN:
        g->sel += HEX_BYTES;
        if (g->sel >= (int)g->size) g->sel = (int)g->size - 1;
        break;
    case KEY_PGUP:
        g->offset -= HEX_ROWS_VIS * HEX_BYTES;
        if (g->offset < 0) g->offset = 0;
        break;
    case KEY_PGDN:
        g->offset += HEX_ROWS_VIS * HEX_BYTES;
        break;
    default: break;
    }
    // keep selection visible
    if (g->sel < g->offset) g->offset = g->sel;
    if (g->sel >= g->offset + HEX_ROWS_VIS * HEX_BYTES)
        g->offset = (g->sel / HEX_BYTES - HEX_ROWS_VIS + 1) * HEX_BYTES;
    if (g->offset < 0) g->offset = 0;
}

void hex_close(Window* w) {
    HexEdit* g = (HexEdit*)w->userdata;
    if (g) {
        if (g->data) kfree(g->data);
        delete g;
    }
    w->userdata = 0;
}

} // namespace

void hexedit_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Hex Editor", x, y, 380, 400);
    if (!w) return;
    HexEdit* g = new HexEdit();
    g->w = w->content_w;
    g->h = w->content_h;
    // open default file or create one
    if (!hex_load(*g, "/home/user/Documents/sample.bin")) {
        FSNode* f = g_vfs->create_file("/home/user/Documents/sample.bin");
        if (f) {
            static const uint8_t sample[32] = {
                0x4E,0x45,0x46,0x55,0x4F,0x53,0x20,0x48,0x45,0x58,0x20,0x45,0x44,0x49,0x54,0x4F,
                0x52,0x20,0x53,0x41,0x4D,0x50,0x4C,0x45,0x0A,0x00,0x01,0x02,0x03,0xFF,0xFE,0x80
            };
            g_vfs->write_file(f, sample, sizeof(sample));
        }
        hex_load(*g, "/home/user/Documents/sample.bin");
    }
    g->view_ascii = true;
    w->userdata = g;
    w->on_paint = hex_paint;
    w->on_key = hex_key;
    w->on_close = hex_close;
    g_wm->raise(w);
}

} // namespace nefu
