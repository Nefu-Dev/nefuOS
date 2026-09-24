// nefuOS Recycle Bin App
// Shows deleted files in /home/user/.recycle, allows restore or permanent delete

#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../gui/widgets.h"
#include "../klib/klib.h"
#include "../vfs/vfs.h"
#include <cstring>

namespace nefu {

namespace {

struct RecycleState {
    int scroll;
    Button restore_btn;
    Button delete_btn;
    Button empty_btn;
    int selected;

    RecycleState() : scroll(0), selected(-1) {}
};

static int get_recycle_count() {
    FSNode* dir = g_vfs->resolve("/home/user/.recycle");
    if (!dir || !dir->is_dir()) return 0;
    int count = 0;
    for (int i = 0; i < dir->children.size(); i++) count++;
    return count;
}

static void recycle_paint(Window* w) {
    RecycleState* st = (RecycleState*)w->userdata;
    Surface& s = w->back;

    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, 0xFFFFFF);

    // Title
    gfx::text(s, 10, 10, "Recycle Bin", 0x000000, 0xFFFFFF);

    FSNode* dir = g_vfs->resolve("/home/user/.recycle");
    if (!dir || !dir->is_dir()) {
        gfx::text(s, 10, 40, "Recycle bin not found", 0x666666, 0xFFFFFF);
        return;
    }

    // List files
    int y = 40;
    for (int i = 0; i < dir->children.size(); i++) {
        FSNode* child = dir->children[i];
        if (y > w->content_h - 80) break;

        uint32_t color = (i == st->selected) ? 0x0066CC : 0x000000;
        gfx::text(s, 10, y, child->name.c_str(), color, 0xFFFFFF);
        y += 18;
    }

    if (dir->children.size() == 0) {
        gfx::text(s, 10, 60, "Recycle bin is empty", 0x999999, 0xFFFFFF);
    }

    // Buttons
    st->restore_btn.x = 10;
    st->restore_btn.y = w->content_h - 35;
    st->restore_btn.w = 80;
    st->restore_btn.h = 26;
    st->restore_btn.label = "Restore";
    ui::draw_button(s, st->restore_btn);

    st->delete_btn.x = 100;
    st->delete_btn.y = w->content_h - 35;
    st->delete_btn.w = 100;
    st->delete_btn.h = 26;
    st->delete_btn.label = "Delete Forever";
    ui::draw_button(s, st->delete_btn);

    st->empty_btn.x = 210;
    st->empty_btn.y = w->content_h - 35;
    st->empty_btn.w = 80;
    st->empty_btn.h = 26;
    st->empty_btn.label = "Empty All";
    ui::draw_button(s, st->empty_btn);
}

static void recycle_mouse(Window* w, int mx, int my, uint8_t buttons) {
    RecycleState* st = (RecycleState*)w->userdata;
    bool pressed = buttons;

    // Button clicks
    st->restore_btn.id = 0;
    if (ui::button_event(st->restore_btn, mx, my, buttons, pressed, false)) {
        // Restore selected file
    }

    st->delete_btn.id = 1;
    if (ui::button_event(st->delete_btn, mx, my, buttons, pressed, false)) {
        // Delete forever
    }

    st->empty_btn.id = 2;
    if (ui::button_event(st->empty_btn, mx, my, buttons, pressed, false)) {
        // Empty all
        FSNode* dir = g_vfs->resolve("/home/user/.recycle");
        if (dir) {
            for (int i = dir->children.size() - 1; i >= 0; i--) {
                g_vfs->remove_node(dir->children[i]);
            }
        }
    }

    // File selection
    if (pressed && my >= 40 && my < w->content_h - 40) {
        int idx = (my - 40) / 18;
        FSNode* dir = g_vfs->resolve("/home/user/.recycle");
        if (dir && idx >= 0 && idx < dir->children.size()) {
            st->selected = idx;
        }
    }
}

static void recycle_close(Window* w) {
    RecycleState* st = (RecycleState*)w->userdata;
    delete st;
}

} // namespace

void recycle_launch() {
    RecycleState* st = new RecycleState();
    Window* w = g_wm->create_window("Recycle Bin", 200, 150, 400, 350);
    w->userdata = st;
    w->on_paint = recycle_paint;
    w->on_mouse = recycle_mouse;
    w->on_close = recycle_close;
    g_wm->raise(w);
}

} // namespace nefu