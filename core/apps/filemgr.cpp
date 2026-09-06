// nefuOS File Manager v3: context menu, trash, drag&drop, sort, views
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/widgets.h"
#include "../platform.h"

namespace nefu {

struct TreeRow { FSNode* n; int depth; };

enum {
    MENU_OPEN = 0, MENU_RENAME, MENU_TRASH, MENU_DELETE, MENU_RESTORE,
    MENU_EMPTY_TRASH, MENU_PROPS, MENU_N
};

struct FMState {
    FSNode* sel_dir;
    String sel_file;
    int tree_scroll;
    int list_scroll;
    int last_click_pane;
    int last_click_row;
    uint32_t last_click_time;
    Window* win;
    List<FSNode*> hist_back;
    List<FSNode*> hist_fwd;
    int sort_mode;
    int sort_desc;
    int view_mode;
    Button btns[6];
    Button* cur;
    uint8_t last_buttons;
    // context menu
    bool menu_open;
    int menu_x, menu_y, menu_hover;
    FSNode* menu_node;
    // rename
    bool renaming;
    String rename_buf;
    // drag & drop
    bool dragging;
    FSNode* drag_node;
    int drag_pane;
    FSNode* drop_target;
    char status_msg[128];
    FMState() : menu_open(false), menu_x(0), menu_y(0), menu_hover(-1), menu_node(0),
                renaming(false), dragging(false), drag_node(0), drag_pane(-1), drop_target(0) {
        status_msg[0] = 0;
    }
};

static const uint32_t FM_BG = 0x00FAF9F6;
static const uint32_t FM_HEAD = 0x00E7E6E1;
static const uint32_t FM_SEL = 0x00D8E6F5;
static const int TOOL_H = 34;
static const int HEAD_H = 18;
static const int STAT_H = 20;

static const char* file_ext(FSNode* n) {
    int i = n->name.rfind('.');
    return i >= 0 ? n->name.c_str() + i + 1 : "";
}

static int node_cmp(FSNode* a, FSNode* b, int mode) {
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    int r = 0;
    if (mode == 1) {
        if (a->size != b->size) r = a->size < b->size ? -1 : 1;
        else r = strcmp(a->name.c_str(), b->name.c_str());
    } else if (mode == 2) {
        r = strcmp(file_ext(a), file_ext(b));
        if (r == 0) r = strcmp(a->name.c_str(), b->name.c_str());
    } else {
        r = strcmp(a->name.c_str(), b->name.c_str());
    }
    return r;
}

static void sort_children(List<FSNode*>& list, int mode, int desc) {
    for (int i = 1; i < list.size(); i++) {
        FSNode* key = list[i];
        int j = i - 1;
        while (j >= 0) {
            int c = node_cmp(list[j], key, mode);
            if (desc && !list[j]->is_dir && !key->is_dir) c = -c;
            if (c <= 0) break;
            list[j + 1] = list[j];
            j--;
        }
        list[j + 1] = key;
    }
}

static void tree_collect(FSNode* n, int depth, List<TreeRow>& out, int limit) {
    if (out.size() >= limit) return;
    TreeRow r;
    r.n = n;
    r.depth = depth;
    out.push(r);
    if (n->is_dir && n->expanded) {
        for (int i = 0; i < n->children.size(); i++) tree_collect(n->children[i], depth + 1, out, limit);
    }
}

static void fm_cd(FMState* st, FSNode* dir, bool push_back) {
    if (!dir || !dir->is_dir) return;
    if (push_back && st->sel_dir != dir) st->hist_back.push(st->sel_dir);
    st->hist_fwd.clear();
    st->sel_dir = dir;
    st->sel_file.clear();
    st->list_scroll = 0;
    st->menu_open = false;
    st->renaming = false;
    // expand the whole ancestor chain so the current directory is visible
    // in the tree pane without digging down from the root manually
    FSNode* a = dir;
    while (a && a != g_vfs->root()) {
        a->expanded = true;
        a = a->parent;
    }
    dir->expanded = true;
}

static bool in_trash_dir(FSNode* d) {
    FSNode* td = g_vfs->trash_dir();
    return d && td && (d == td || d->parent == td);
}

// hide kernel pseudo filesystems and dot-files from the content pane
static bool fm_visible(FSNode* n) {
    if (n->name.len() > 0 && n->name[0] == '.') return false;
    if (n->parent == g_vfs->root()) {
        if (strcmp(n->name.c_str(), "proc") == 0) return false;
        if (strcmp(n->name.c_str(), "sys") == 0) return false;
        if (strcmp(n->name.c_str(), "dev") == 0) return false;
        if (strcmp(n->name.c_str(), "boot") == 0) return false;
        if (strcmp(n->name.c_str(), "mnt") == 0) return false;
    }
    return true;
}

static void fm_open(FSNode* c) {
    if (c->is_dir) return;
    const char* ext = file_ext(c);
    if (strcmp(ext, "ppm") == 0 || strcmp(ext, "pbm") == 0 || strcmp(ext, "bmp") == 0 ||
            strcmp(ext, "jpg") == 0 || strcmp(ext, "jpeg") == 0 || strcmp(ext, "png") == 0 ||
            strcmp(ext, "gif") == 0 || strcmp(ext, "img") == 0) app_show_image(c);
    else if (strcmp(ext, "nefud") == 0 || strcmp(ext, "bin") == 0) app_show_nefud(c);
    else app_show_textview(c);
}

// ---------- context menu actions ----------
static void menu_do(FMState* st, int action) {
    st->menu_open = false;
    if (!st->menu_node) return;
    FSNode* n = st->menu_node;
    if (action == MENU_OPEN) {
        if (n->is_dir) fm_cd(st, n, true);
        else fm_open(n);
    } else if (action == MENU_RENAME) {
        st->renaming = true;
        st->rename_buf = n->name;
    } else if (action == MENU_TRASH) {
        if (!n->is_dir && g_vfs->trash_file(n)) {
            ksprintf(st->status_msg, sizeof(st->status_msg), "Moved to Trash: %s", n->name.c_str());
            st->sel_file.clear();
        }
    } else if (action == MENU_DELETE) {
        if (n->is_dir && g_vfs->remove_node(n)) {
            ksprintf(st->status_msg, sizeof(st->status_msg), "Deleted folder: %s", n->name.c_str());
            st->sel_file.clear();
        }
    } else if (action == MENU_RESTORE) {
        if (g_vfs->restore_file(n, g_vfs->resolve("/home/user/Documents"))) {
            ksprintf(st->status_msg, sizeof(st->status_msg), "Restored: %s -> Documents", n->name.c_str());
            st->sel_file.clear();
        }
    } else if (action == MENU_EMPTY_TRASH) {
        int c = g_vfs->empty_trash();
        ksprintf(st->status_msg, sizeof(st->status_msg), "Trash emptied: %d file(s)", c);
    } else if (action == MENU_PROPS) {
        char buf[160];
        if (n->is_dir) ksprintf(buf, sizeof(buf), "%s/  folder", n->name.c_str());
        else ksprintf(buf, sizeof(buf), "%s  %u bytes  .%s", n->name.c_str(),
                      (unsigned)n->size, file_ext(n));
        strncpy(st->status_msg, buf, sizeof(st->status_msg) - 1);
    }
    if (action != MENU_RENAME) st->menu_node = 0;
}

// ---------- paint ----------
static void fm_paint(Window* w) {
    FMState* st = (FMState*)w->userdata;
    Surface& s = w->back;
    int W = s.width, H = s.height;
    int left_w = W * 2 / 5;
    s.fill(FM_BG);

    gfx::fillrect(s, 0, 0, W, TOOL_H, FM_HEAD);
    gfx::hline(s, 0, W - 1, TOOL_H, color::BORDER);
    for (int i = 0; i < 6; i++) ui::draw_button(s, st->btns[i]);
    String path = node_path(st->sel_dir ? st->sel_dir : g_vfs->root());
    int px = 6 + 6 * 44 + 8;
    gfx::fillrect(s, px, 5, W - px - 10, 24, color::WHITE);
    gfx::rect(s, px, 5, W - px - 10, 24, 0x00B0AFA8);
    gfx::text(s, px + 6, 9, path.c_str(), color::BLUE, color::WHITE);

    // tree
    gfx::fillrect(s, 0, TOOL_H, left_w, 24, FM_HEAD);
    gfx::text(s, 6, TOOL_H + 4, "Folders", color::TEXT, FM_HEAD);
    gfx::vline(s, left_w, TOOL_H, H - STAT_H, 0x00D8D7D1);
    List<TreeRow> rows;
    tree_collect(g_vfs->root(), 0, rows, 2048);
    int vis = (H - TOOL_H - 24 - STAT_H) / 16;
    if (st->tree_scroll > rows.size() - vis && rows.size() - vis > 0) st->tree_scroll = rows.size() - vis;
    for (int i = st->tree_scroll; i < rows.size() && i < st->tree_scroll + vis; i++) {
        int y = TOOL_H + 24 + (i - st->tree_scroll) * 16;
        TreeRow& r = rows[i];
        uint32_t rowbg = (r.n == st->sel_dir) ? FM_SEL : FM_BG;
        gfx::fillrect(s, 0, y, left_w, 16, rowbg);
        int x = 6 + r.depth * 12;
        if (r.n->is_dir) {
            gfx::text(s, x, y + 1, r.n->expanded ? "[-]" : "[+]", color::TEXT2, rowbg);
            gfx::text(s, x + 24, y + 1, r.n->name.c_str(), color::BLUE, rowbg);
        } else {
            gfx::text(s, x + 12, y + 1, r.n->name.c_str(), color::TEXT, rowbg);
        }
    }

    // content
    int rx = left_w + 1;
    int rw = W - rx;
    FSNode* d = st->sel_dir ? st->sel_dir : g_vfs->root();
    List<FSNode*> view;
    for (int i = 0; i < d->children.size(); i++) {
        if (fm_visible(d->children[i])) view.push(d->children[i]);
    }
    sort_children(view, st->sort_mode, st->sort_desc);

    if (st->view_mode == 0) {
        gfx::fillrect(s, rx, TOOL_H, rw, HEAD_H, FM_HEAD);
        gfx::hline(s, rx, W - 1, TOOL_H + HEAD_H, color::BORDER);
        const char* cols[3] = { "Name", "Size", "Type" };
        for (int i = 0; i < 3; i++) {
            int cx = rx + 8 + i * 130;
            uint32_t cc = (st->sort_mode == i) ? color::BLUE : color::TEXT;
            gfx::text(s, cx, TOOL_H + 2, cols[i], cc, FM_HEAD);
            if (st->sort_mode == i) gfx::text(s, cx + gfx::text_width(cols[i]) + 4, TOOL_H + 2,
                                              st->sort_desc ? "^" : "v", color::BLUE, FM_HEAD);
        }
        int rvis = (H - TOOL_H - HEAD_H - STAT_H) / 16;
        for (int i = st->list_scroll; i < view.size() && i < st->list_scroll + rvis; i++) {
            int y = TOOL_H + HEAD_H + (i - st->list_scroll) * 16;
            FSNode* c = view[i];
            bool sel = (st->sel_file == c->name);
            bool drop = (st->drop_target == c);
            uint32_t bg = sel ? FM_SEL : (drop ? 0x00FFF3CC : FM_BG);
            gfx::fillrect(s, rx, y, rw, 16, bg);
            if (drop) gfx::rect(s, rx, y, rw - 1, 16, color::ORANGE);
            gfx::text(s, rx + 4, y + 1, c->is_dir ? "DIR " : "FILE", c->is_dir ? color::ORANGE : color::TEXT2, bg);
            gfx::text(s, rx + 44, y + 1, c->name.c_str(), c->is_dir ? color::BLUE : color::TEXT, bg);
            if (!c->is_dir) {
                char sz[24];
                if (c->size >= 1024) ksprintf(sz, sizeof(sz), "%u KB", (unsigned)(c->size / 1024));
                else ksprintf(sz, sizeof(sz), "%u B", (unsigned)c->size);
                gfx::text(s, rx + 140, y + 1, sz, color::TEXT2, bg);
                gfx::text(s, rx + 240, y + 1, file_ext(c), color::TEXT2, bg);
            } else {
                gfx::text(s, rx + 140, y + 1, "-", color::TEXT2, bg);
                gfx::text(s, rx + 240, y + 1, "folder", color::TEXT2, bg);
            }
        }
        if (st->renaming) {
            int y = TOOL_H + HEAD_H + 2;
            gfx::fillrect(s, rx + 2, y, 220, 20, 0x00F0EFEA);
            gfx::rect(s, rx + 2, y, 220, 20, color::BLUE_LT);
            gfx::text(s, rx + 6, y + 2, st->rename_buf.c_str(), color::TEXT, 0x00F0EFEA);
        }
    } else {
        int cols = rw / 96;
        if (cols < 1) cols = 1;
        int base_y = TOOL_H + 6;
        for (int i = st->list_scroll; i < view.size(); i++) {
            int cell = i - st->list_scroll;
            int gx = rx + (cell % cols) * 96;
            int gy = base_y + (cell / cols) * 78;
            if (gy + 78 > H - STAT_H) break;
            FSNode* c = view[i];
            bool sel = (st->sel_file == c->name);
            bool drop = (st->drop_target == c);
            uint32_t bg = sel ? FM_SEL : (drop ? 0x00FFF3CC : FM_BG);
            gfx::fillrect(s, gx, gy, 92, 74, bg);
            if (sel) gfx::rect(s, gx, gy, 92, 74, color::BLUE_LT);
            if (c->is_dir) {
                gfx::fillrect(s, gx + 20, gy + 4, 52, 44, color::YELLOW);
                gfx::rect(s, gx + 20, gy + 4, 52, 44, 0x008C7A20);
                gfx::fillrect(s, gx + 20, gy + 2, 18, 6, color::YELLOW);
                gfx::rect(s, gx + 20, gy + 2, 18, 6, 0x008C7A20);
            } else if (strcmp(file_ext(c), "bin") == 0) {
                // .bin: Linux-terminal style icon (dark square, white ">_")
                gfx::fillrect(s, gx + 20, gy + 4, 52, 44, 0x00222B35);
                gfx::rect(s, gx + 20, gy + 4, 52, 44, 0x003B4A5A);
                gfx::text(s, gx + 32, gy + 20, ">_", color::WHITE, 0x00222B35);
                gfx::text(s, gx + 34, gy + 32, "bin", 0x008CA3B8, 0x00222B35);
            } else {
                gfx::fillrect(s, gx + 20, gy + 4, 52, 44, 0x00D9DEE8);
                gfx::rect(s, gx + 20, gy + 4, 52, 44, 0x009AA6B4);
                gfx::text(s, gx + 36, gy + 22, file_ext(c), color::TEXT2, 0x00D9DEE8);
            }
            String nm = c->name;
            if (nm.len() > 10) nm = nm.substr(0, 10);
            int tw = gfx::text_width(nm.c_str());
            gfx::text(s, gx + (92 - tw) / 2, gy + 56, nm.c_str(), color::TEXT, bg);
        }
    }

    // status bar
    gfx::fillrect(s, 0, H - STAT_H, W, STAT_H, FM_HEAD);
    gfx::hline(s, 0, W - 1, H - STAT_H, color::BORDER);
    char buf[96];
    int dirs = 0, files = 0;
    for (int i = 0; i < d->children.size(); i++) {
        if (d->children[i]->is_dir) dirs++; else files++;
    }
    if (st->status_msg[0]) {
        gfx::text(s, 8, H - STAT_H + 3, st->status_msg, color::BLUE, FM_HEAD);
    } else {
        ksprintf(buf, sizeof(buf), "%d items  (%d dirs, %d files)", d->children.size(), dirs, files);
        gfx::text(s, 8, H - STAT_H + 3, buf, color::TEXT2, FM_HEAD);
        if (!st->sel_file.empty()) {
            ksprintf(buf, sizeof(buf), "Selected: %s", st->sel_file.c_str());
            gfx::text(s, W - gfx::text_width(buf) - 10, H - STAT_H + 3, buf, color::BLUE, FM_HEAD);
        }
    }

    // context menu
    if (st->menu_open) {
        const char* items[MENU_N];
        if (st->menu_node && st->menu_node->is_dir && in_trash_dir(st->sel_dir)) {
            items[MENU_OPEN] = "Open"; items[MENU_RENAME] = "Rename";
            items[MENU_TRASH] = "Delete folder"; items[MENU_DELETE] = "Delete";
            items[MENU_RESTORE] = "Restore"; items[MENU_EMPTY_TRASH] = "Empty Trash";
            items[MENU_PROPS] = "Properties";
        } else if (st->menu_node && st->menu_node->is_dir) {
            items[MENU_OPEN] = "Open"; items[MENU_RENAME] = "Rename";
            items[MENU_TRASH] = "Move to Trash"; items[MENU_DELETE] = "Delete folder";
            items[MENU_RESTORE] = "Restore"; items[MENU_EMPTY_TRASH] = "Empty Trash";
            items[MENU_PROPS] = "Properties";
        } else {
            items[MENU_OPEN] = "Open"; items[MENU_RENAME] = "Rename";
            items[MENU_TRASH] = "Move to Trash"; items[MENU_DELETE] = "Delete";
            items[MENU_RESTORE] = "Restore"; items[MENU_EMPTY_TRASH] = "Empty Trash";
            items[MENU_PROPS] = "Properties";
        }
        int mw = 180, mh = MENU_N * 22 + 6;
        int mx = st->menu_x, my = st->menu_y;
        if (mx + mw > W) mx = W - mw - 4;
        if (my + mh > H - STAT_H) my = H - STAT_H - mh - 4;
        gfx::fillrect(s, mx, my, mw, mh, color::WHITE);
        gfx::rect(s, mx, my, mw, mh, color::BORDER);
        for (int i = 0; i < MENU_N; i++) {
            int iy = my + 3 + i * 22;
            if (st->menu_hover == i) gfx::fillrect(s, mx + 1, iy, mw - 2, 22, FM_SEL);
            gfx::text(s, mx + 10, iy + 3, items[i], color::TEXT, (st->menu_hover == i) ? FM_SEL : color::WHITE);
        }
    }
}

// ---------- mouse ----------
static void fm_mouse(Window* w, int mx, int my, uint8_t buttons) {
    FMState* st = (FMState*)w->userdata;
    int W = w->content_w, H = w->content_h;
    int left_w = W * 2 / 5;
    uint32_t now = platform_tick_ms();
    bool pressed = buttons && !st->last_buttons;
    bool released = !buttons && st->last_buttons;
    bool r_pressed = (buttons & 0x02) && !(st->last_buttons & 0x02);
    st->last_buttons = buttons;

    for (int i = 0; i < 6; i++) {
        st->cur = &st->btns[i];
        ui::button_event(st->btns[i], mx, my, buttons, pressed, released);
    }
    st->cur = 0;

    // context menu open: handle clicks on it
    if (st->menu_open) {
        int mw = 180, mh = MENU_N * 22 + 6;
        if (mx >= st->menu_x && mx < st->menu_x + mw && my >= st->menu_y && my < st->menu_y + mh) {
            int idx = (my - st->menu_y - 3) / 22;
            if (idx >= 0 && idx < MENU_N) st->menu_hover = idx;
            if (pressed) { st->menu_hover = idx; }
            if (released && idx >= 0 && idx < MENU_N) menu_do(st, idx);
            return;
        }
        if (pressed) { st->menu_open = false; st->menu_node = 0; }
    }

    // right click opens menu
    if (r_pressed) {
        st->menu_open = false;
        st->menu_node = 0;
        if (my >= TOOL_H && my < H - STAT_H) {
            FSNode* d = st->sel_dir ? st->sel_dir : g_vfs->root();
            if (mx < left_w) {
                List<TreeRow> rows;
                tree_collect(g_vfs->root(), 0, rows, 2048);
                int idx = st->tree_scroll + (my - TOOL_H - 24) / 16;
                if (idx >= 0 && idx < rows.size()) {
                    st->menu_node = rows[idx].n;
                    st->menu_x = mx;
                    st->menu_y = my;
                    st->menu_hover = -1;
                    st->menu_open = true;
                }
            } else {
                List<FSNode*> view;
                for (int i = 0; i < d->children.size(); i++) view.push(d->children[i]);
                sort_children(view, st->sort_mode, st->sort_desc);
                int idx;
                if (st->view_mode == 0) idx = st->list_scroll + (my - TOOL_H - HEAD_H) / 16;
                else {
                    int cols = (W - (left_w + 1)) / 96; if (cols < 1) cols = 1;
                    idx = st->list_scroll + (my - TOOL_H - 6) / 78 * cols + (mx - (left_w + 1)) / 96;
                }
                if (idx >= 0 && idx < view.size()) {
                    st->menu_node = view[idx];
                    st->sel_file = view[idx]->name;
                    st->menu_x = mx;
                    st->menu_y = my;
                    st->menu_hover = -1;
                    st->menu_open = true;
                }
            }
        }
        return;
    }

    if (!pressed && !released && !buttons) {
        st->drop_target = 0;
        return;
    }

    if (!pressed) return;

    if (my < TOOL_H) return;
    if (my >= H - STAT_H) return;

    // drag start: press on content row without double click
    if (mx >= left_w && my >= TOOL_H + HEAD_H && st->view_mode == 0) {
        FSNode* d = st->sel_dir ? st->sel_dir : g_vfs->root();
        List<FSNode*> view;
        for (int i = 0; i < d->children.size(); i++) view.push(d->children[i]);
        sort_children(view, st->sort_mode, st->sort_desc);
        int idx = st->list_scroll + (my - TOOL_H - HEAD_H) / 16;
        if (idx >= 0 && idx < view.size()) {
            FSNode* c = view[idx];
            bool dbl = (st->last_click_pane == 1 && st->last_click_row == idx &&
                        now - st->last_click_time < 400);
            st->last_click_pane = 1;
            st->last_click_row = idx;
            st->last_click_time = now;
            if (dbl) {
                if (c->is_dir) fm_cd(st, c, true);
                else fm_open(c);
                return;
            }
            st->sel_file = c->name;
            st->dragging = true;
            st->drag_node = c;
            st->drag_pane = 1;
            st->status_msg[0] = 0;
            return;
        }
    }
    if (mx < left_w) {
        List<TreeRow> rows;
        tree_collect(g_vfs->root(), 0, rows, 2048);
        int idx = st->tree_scroll + (my - TOOL_H - 24) / 16;
        if (idx < 0 || idx >= rows.size()) return;
        TreeRow& r = rows[idx];
        bool dbl = (st->last_click_pane == 0 && st->last_click_row == idx &&
                    now - st->last_click_time < 400);
        st->last_click_pane = 0;
        st->last_click_row = idx;
        st->last_click_time = now;
        if (r.n->is_dir) {
            fm_cd(st, r.n, false);
            if (dbl) r.n->expanded = !r.n->expanded;
        } else {
            st->sel_file = r.n->name;
            if (dbl) fm_open(r.n);
        }
        return;
    }
    // icon view click
    if (st->view_mode == 1) {
        FSNode* d = st->sel_dir ? st->sel_dir : g_vfs->root();
        List<FSNode*> view;
        for (int i = 0; i < d->children.size(); i++) view.push(d->children[i]);
        sort_children(view, st->sort_mode, st->sort_desc);
        int cols = (W - (left_w + 1)) / 96; if (cols < 1) cols = 1;
        int cell = (my - TOOL_H - 6) / 78 * cols + (mx - (left_w + 1)) / 96;
        int idx = st->list_scroll + cell;
        if (idx < 0 || idx >= view.size()) return;
        FSNode* c = view[idx];
        bool dbl = (st->last_click_pane == 1 && st->last_click_row == idx &&
                    now - st->last_click_time < 400);
        st->last_click_pane = 1;
        st->last_click_row = idx;
        st->last_click_time = now;
        if (dbl) {
            if (c->is_dir) fm_cd(st, c, true);
            else fm_open(c);
        } else {
            st->sel_file = c->name;
            st->dragging = true;
            st->drag_node = c;
            st->drag_pane = 1;
        }
    }
}

// called every frame while dragging (from WM move events)
static void fm_drag_update(Window* w, int mx, int my) {
    FMState* st = (FMState*)w->userdata;
    if (!st->dragging || !st->drag_node) return;
    st->drop_target = 0;
    if (!st->win) return;
    int W = w->content_w, H = w->content_h;
    int left_w = W * 2 / 5;
    if (mx >= left_w && my >= TOOL_H + HEAD_H && my < H - STAT_H && st->view_mode == 0) {
        FSNode* d = st->sel_dir ? st->sel_dir : g_vfs->root();
        List<FSNode*> view;
        for (int i = 0; i < d->children.size(); i++) view.push(d->children[i]);
        sort_children(view, st->sort_mode, st->sort_desc);
        int idx = st->list_scroll + (my - TOOL_H - HEAD_H) / 16;
        if (idx >= 0 && idx < view.size() && view[idx]->is_dir && view[idx] != st->drag_node)
            st->drop_target = view[idx];
    } else if (mx < left_w && my >= TOOL_H + 24) {
        List<TreeRow> rows;
        tree_collect(g_vfs->root(), 0, rows, 2048);
        int idx = st->tree_scroll + (my - TOOL_H - 24) / 16;
        if (idx >= 0 && idx < rows.size() && rows[idx].n->is_dir && rows[idx].n != st->drag_node)
            st->drop_target = rows[idx].n;
    }
}

static void fm_drag_drop(Window* w) {
    FMState* st = (FMState*)w->userdata;
    if (!st->dragging || !st->drag_node) { st->dragging = false; return; }
    FSNode* src = st->drag_node;
    FSNode* dst = st->drop_target;
    st->dragging = false;
    st->drop_target = 0;
    if (!dst) return;
    if (dst == src || dst == src->parent) return;
    // protect system dirs at root
    if (src->is_dir && src->parent == g_vfs->root()) {
        ksprintf(st->status_msg, sizeof(st->status_msg), "Cannot move system folder: %s", src->name.c_str());
        return;
    }
    if (g_vfs->move_node(src, dst)) {
        String sp = node_path(src);
        ksprintf(st->status_msg, sizeof(st->status_msg), "Moved: %s -> %s/", src->name.c_str(), dst->name.c_str());
        st->sel_file.clear();
    } else {
        ksprintf(st->status_msg, sizeof(st->status_msg), "Move failed");
    }
}

static void fm_scroll(Window* w, int delta) {
    FMState* st = (FMState*)w->userdata;
    int step = delta > 0 ? -2 : 2;
    st->tree_scroll += step;
    st->list_scroll += step;
    if (st->tree_scroll < 0) st->tree_scroll = 0;
    if (st->list_scroll < 0) st->list_scroll = 0;
}

static void fm_key(Window* w, const KeyEvent* e) {
    FMState* st = (FMState*)w->userdata;
    if (!e->down) return;
    if (st->renaming) {
        if (e->ascii >= 32 && e->ascii < 127) {
            if (st->rename_buf.len() < 96) st->rename_buf += e->ascii;
        } else if (e->keycode == KEY_SPACE) {
            if (st->rename_buf.len() < 96) st->rename_buf += ' ';
        } else if (e->keycode == KEY_BACKSPACE) {
            if (st->rename_buf.len() > 0) st->rename_buf = st->rename_buf.substr(0, st->rename_buf.len() - 1);
        } else if (e->keycode == KEY_ENTER) {
            if (st->menu_node && !st->rename_buf.empty() &&
                strcmp(st->rename_buf.c_str(), st->menu_node->name.c_str()) != 0) {
                FSNode* p = st->menu_node->parent;
                g_vfs->move_node(st->menu_node, p, st->rename_buf.c_str());
                st->sel_file = st->rename_buf;
                ksprintf(st->status_msg, sizeof(st->status_msg), "Renamed: %s", st->rename_buf.c_str());
            }
            st->renaming = false;
            st->menu_node = 0;
        } else if (e->keycode == KEY_ESC) {
            st->renaming = false;
            st->menu_node = 0;
        }
        return;
    }
    if (e->keycode == KEY_DEL && !st->sel_file.empty()) {
        FSNode* d = st->sel_dir ? st->sel_dir : g_vfs->root();
        for (int i = 0; i < d->children.size(); i++) {
            if (d->children[i]->name == st->sel_file) {
                FSNode* c = d->children[i];
                if (c->is_dir) {
                    if (c->parent != g_vfs->root() && g_vfs->remove_node(c))
                        ksprintf(st->status_msg, sizeof(st->status_msg), "Deleted folder: %s", c->name.c_str());
                    else
                        ksprintf(st->status_msg, sizeof(st->status_msg), "Cannot delete system folder");
                } else if (g_vfs->trash_file(c)) {
                    ksprintf(st->status_msg, sizeof(st->status_msg), "Moved to Trash: %s", c->name.c_str());
                }
                st->sel_file.clear();
                return;
            }
        }
    }
    if (e->keycode == KEY_ENTER && !st->sel_file.empty()) {
        FSNode* d = st->sel_dir ? st->sel_dir : g_vfs->root();
        for (int i = 0; i < d->children.size(); i++) {
            if (d->children[i]->name == st->sel_file) {
                FSNode* c = d->children[i];
                if (c->is_dir) fm_cd(st, c, true);
                else fm_open(c);
                return;
            }
        }
    }
}

static void fm_click(void* ud) {
    FMState* st = (FMState*)ud;
    if (!st->cur) return;
    const char* lab = st->cur->label;
    if (strcmp(lab, "Back") == 0) {
        if (st->hist_back.size() > 0) {
            st->hist_fwd.push(st->sel_dir);
            st->sel_dir = st->hist_back.pop();
            st->sel_file.clear();
            st->list_scroll = 0;
        }
    } else if (strcmp(lab, "Fwd") == 0) {
        if (st->hist_fwd.size() > 0) {
            st->hist_back.push(st->sel_dir);
            st->sel_dir = st->hist_fwd.pop();
            st->sel_file.clear();
            st->list_scroll = 0;
        }
    } else if (strcmp(lab, "Up") == 0) {
        if (st->sel_dir && st->sel_dir->parent) fm_cd(st, st->sel_dir->parent, true);
    } else if (strcmp(lab, "Ref") == 0) {
        st->sel_file.clear();
        st->status_msg[0] = 0;
    } else if (strcmp(lab, "List") == 0) {
        st->view_mode = 0;
        st->list_scroll = 0;
    } else if (strcmp(lab, "Icons") == 0) {
        st->view_mode = 1;
        st->list_scroll = 0;
    }
}

static void fm_close(Window* w) {
    if (w->userdata) delete (FMState*)w->userdata;
    w->userdata = 0;
}

void fm_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("File Manager", x, y, 680, 440);
    if (!w) return;
    FMState* st = new FMState();
    st->win = w;
    st->sel_dir = g_vfs->root();
    st->tree_scroll = 0;
    st->list_scroll = 0;
    st->last_click_pane = -1;
    st->last_click_row = -1;
    st->last_click_time = 0;
    st->sort_mode = 0;
    st->sort_desc = 0;
    st->view_mode = 0;
    st->cur = 0;
    st->last_buttons = 0;
    const char* labels[6] = { "Back", "Fwd", "Up", "Ref", "List", "Icons" };
    for (int i = 0; i < 6; i++) {
        Button& b = st->btns[i];
        b.x = 6 + i * 44;
        b.y = 5;
        b.w = 38;
        b.h = 24;
        b.label = labels[i];
        b.id = i;
        b.pressed = false;
        b.on_click = fm_click;
        b.ud = st;
    }
    w->userdata = st;
    w->on_paint = fm_paint;
    w->on_mouse = fm_mouse;
    w->on_scroll = fm_scroll;
    w->on_key = fm_key;
    w->on_close = fm_close;
    w->on_drag = fm_drag_update;
    w->on_drop = fm_drag_drop;
}

} // namespace nefu
