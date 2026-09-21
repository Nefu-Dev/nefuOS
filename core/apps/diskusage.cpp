// nefuOS Disk Usage Analyzer
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../vfs/vfs.h"
#include "../platform.h"

namespace nefu {

struct DiskUsageState {
    int w, h;
};

static void count_nodes(FSNode* node, int* files, int* dirs, uint32_t* total_bytes) {
    if (!node) return;
    if (node->is_dir) (*dirs)++;
    else { (*files)++; *total_bytes += node->size; }
    for (int i = 0; i < node->children.size(); i++) {
        count_nodes(node->children[i], files, dirs, total_bytes);
    }
}

static void diskusage_paint(Window* win) {
    DiskUsageState* st = (DiskUsageState*)win->userdata;
    Surface& cs = win->back;
    cs.fill(0x00F5F5F0);

    gfx::text(cs, 10, 8, "Disk Usage Analyzer", color::TEXT, 0x00F5F5F0);

    int files = 0, dirs = 0;
    uint32_t total = 0;
    count_nodes(g_vfs->root(), &files, &dirs, &total);

    char buf[128];
    int y = 36;

    ksprintf(buf, sizeof(buf), "Total files:  %d", files);
    gfx::text(cs, 10, y, buf, color::TEXT, 0x00F5F5F0); y += 22;

    ksprintf(buf, sizeof(buf), "Total dirs:   %d", dirs);
    gfx::text(cs, 10, y, buf, color::TEXT, 0x00F5F5F0); y += 22;

    ksprintf(buf, sizeof(buf), "Total size:   %u bytes", total);
    gfx::text(cs, 10, y, buf, color::TEXT, 0x00F5F5F0); y += 22;

    uint32_t kb = total / 1024;
    uint32_t mb = kb / 1024;
    ksprintf(buf, sizeof(buf), "              ~%u KB (~%u MB)", kb, mb);
    gfx::text(cs, 10, y, buf, 0x00666666, 0x00F5F5F0); y += 30;

    // Show top-level dirs
    gfx::text(cs, 10, y, "Top-level directories:", color::TEXT2, 0x00F5F5F0); y += 22;

    for (int i = 0; i < g_vfs->root()->children.size() && y < st->h - 20; i++) {
        FSNode* child = g_vfs->root()->children[i];
        if (!child->is_dir) continue;
        char line[80];
        ksprintf(line, sizeof(line), "  /%s", child->name.c_str());
        gfx::text(cs, 10, y, line, color::TEXT, 0x00F5F5F0);
        y += 18;
    }

    gfx::text(cs, 10, st->h - 24, "nefuOS Disk Usage", 0x00888888, 0x00F5F5F0);
}

void diskusage_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Disk Usage", x, y, 300, 280);
    if (!w) return;
    DiskUsageState* st = new DiskUsageState();
    st->w = w->content_w;
    st->h = w->content_h;
    w->userdata = st;
    w->on_paint = diskusage_paint;
    g_wm->raise(w);
}

} // namespace nefu
