// nefuOS Task Manager / System Monitor enhanced
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../gui/widgets.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {

struct TaskMgrState {
    int w, h;
    int tab; // 0=processes, 1=performance, 2=disk
};

static void taskmgr_paint(Window* win) {
    Surface& s = win->back;
    int W = win->content_w;
    int H = win->content_h;
    gfx::fillrect(s, 0, 0, W, H, 0x1e1e2e);

    TaskMgrState* st = (TaskMgrState*)win->userdata;

    // Tab bar
    gfx::fillrect(s, 0, 0, W, 25, 0x313244);
    const char* tabs[] = {"Processes", "Performance", "Disk"};
    for (int i = 0; i < 3; i++) {
        int tx = 10 + i * 80;
        if (st->tab == i) {
            gfx::fillrect(s, tx, 3, 70, 20, 0x4a9eff);
            gfx::text(s, tx + 10, 8, tabs[i], 0xffffff, 0);
        } else {
            gfx::text(s, tx + 10, 8, tabs[i], 0xcdd6f4, 0);
        }
    }

    int y = 35;
    if (st->tab == 0) {
        // Processes tab
        gfx::text(s, 10, y, "Name                PID    CPU   Memory", 0x89b4fa, 0);
        y += 20;

        const char* procs[][4] = {
            {"nefuOS Kernel", "1", "0.5%", "12 MB"},
            {"Window Manager", "12", "2.1%", "8 MB"},
            {"LVGL Engine", "13", "3.4%", "15 MB"},
            {"File Manager", "42", "1.2%", "6 MB"},
            {"Terminal", "43", "0.8%", "4 MB"},
            {"Browser", "44", "4.5%", "28 MB"},
            {"Music Player", "45", "1.1%", "10 MB"},
            {"System Monitor", "46", "0.3%", "3 MB"},
        };
        for (int i = 0; i < 8 && y < H - 30; i++) {
            gfx::text(s, 10, y, procs[i][0], 0xcdd6f4, 0);
            gfx::text(s, 180, y, procs[i][1], 0xcdd6f4, 0);
            gfx::text(s, 220, y, procs[i][2], 0xa6e3a1, 0);
            gfx::text(s, 280, y, procs[i][3], 0xf9e2af, 0);
            y += 18;
        }
    } else if (st->tab == 1) {
        // Performance tab
        gfx::text(s, 10, y, "CPU Usage:", 0xcdd6f4, 0);
        y += 25;
        gfx::fillrect(s, 10, y, 200, 15, 0x313244);
        gfx::fillrect(s, 10, y, 75, 15, 0xa6e3a1);
        gfx::text(s, 220, y, "35%", 0xa6e3a1, 0);
        y += 30;

        gfx::text(s, 10, y, "Memory Usage:", 0xcdd6f4, 0);
        y += 25;
        gfx::fillrect(s, 10, y, 200, 15, 0x313244);
        gfx::fillrect(s, 10, y, 120, 15, 0xf9e2af);
        gfx::text(s, 220, y, "60%", 0xf9e2af, 0);
        y += 30;

        gfx::text(s, 10, y, "Disk Usage:", 0xcdd6f4, 0);
        y += 25;
        gfx::fillrect(s, 10, y, 200, 15, 0x313244);
        gfx::fillrect(s, 10, y, 40, 15, 0xff6b6b);
        gfx::text(s, 220, y, "20%", 0xff6b6b, 0);
    } else {
        // Disk tab
        gfx::text(s, 10, y, "Disk: nefu0 (nvfs)", 0x89b4fa, 0);
        y += 25;
        gfx::text(s, 20, y, "Size: 64 MB", 0xcdd6f4, 0);
        y += 22;
        gfx::text(s, 20, y, "Used: 12 MB", 0xcdd6f4, 0);
        y += 22;
        gfx::text(s, 20, y, "Free: 52 MB", 0xa6e3a1, 0);
        y += 30;

        gfx::text(s, 10, y, "Partitions:", 0x89b4fa, 0);
        y += 25;
        gfx::text(s, 20, y, "/          (root)      64 MB", 0xcdd6f4, 0);
        y += 22;
        gfx::text(s, 20, y, "/home     (user)      32 MB", 0xcdd6f4, 0);
        y += 22;
        gfx::text(s, 20, y, "/tmp      (temp)        4 MB", 0xcdd6f4, 0);
    }

    gfx::text(s, 10, H - 20, "Press 1/2/3 to switch tabs", 0x6c7086, 0);
}

static void taskmgr_on_key(Window* win, const KeyEvent* e) {
    if (!e->down) return;
    TaskMgrState* st = (TaskMgrState*)win->userdata;

    if (e->ascii >= '1' && e->ascii <= '3') {
        st->tab = e->ascii - '1';
    }
}

void app_taskmgr_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Task Manager", x, y, 450, 320);
    if (!w) return;
    TaskMgrState* st = new TaskMgrState();
    st->w = w->content_w;
    st->h = w->content_h;
    st->tab = 0;
    w->userdata = st;
    w->on_paint = taskmgr_paint;
    w->on_key = taskmgr_on_key;
    g_wm->raise(w);
}

} // namespace nefu
