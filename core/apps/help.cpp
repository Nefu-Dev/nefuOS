// nefuOS built-in help app
// User guide and documentation
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../gui/widgets.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {

struct HelpState {
    int w, h;
    int page;
};

static void help_paint(Window* win) {
    Surface& s = win->back;
    int W = win->content_w;
    int H = win->content_h;
    gfx::fillrect(s, 0, 0, W, H, 0x1e1e2e);

    HelpState* st = (HelpState*)win->userdata;
    int y = 30;

    switch (st->page) {
        case 0: {
            gfx::text(s, 15, y, "=== nefuOS User Guide ===", 0x89b4fa, 0);
            y += 35;
            gfx::text(s, 15, y, "Welcome to nefuOS!", 0xa6e3a1, 0);
            y += 28;
            gfx::text(s, 15, y, "A lightweight C/C++ OS", 0xcdd6f4, 0);
            y += 35;
            gfx::text(s, 15, y, "Version: 0.1 Host Edition", 0xf9e2af, 0);
            y += 35;
            gfx::text(s, 15, y, "Pages:", 0x6c7086, 0);
            y += 25;
            gfx::text(s, 30, y, "1. Desktop Basics", 0xcdd6f4, 0);
            y += 22;
            gfx::text(s, 30, y, "2. Applications", 0xcdd6f4, 0);
            y += 22;
            gfx::text(s, 30, y, "3. Terminal Commands", 0xcdd6f4, 0);
            break;
        }
        case 1: {
            gfx::text(s, 15, y, "=== Desktop Basics ===", 0x89b4fa, 0);
            y += 35;
            gfx::text(s, 15, y, "Mouse:", 0xa6e3a1, 0);
            y += 25;
            gfx::text(s, 35, y, "- Left click: open", 0xcdd6f4, 0);
            y += 22;
            gfx::text(s, 35, y, "- Right click: menu", 0xcdd6f4, 0);
            y += 22;
            gfx::text(s, 35, y, "- Drag: move windows", 0xcdd6f4, 0);
            y += 30;
            gfx::text(s, 15, y, "Taskbar:", 0xa6e3a1, 0);
            y += 25;
            gfx::text(s, 35, y, "- Left: Start + apps", 0xcdd6f4, 0);
            y += 22;
            gfx::text(s, 35, y, "- Right: clock + tray", 0xcdd6f4, 0);
            break;
        }
        case 2: {
            gfx::text(s, 15, y, "=== Applications ===", 0x89b4fa, 0);
            y += 35;
            const char* apps[] = {
                "File Manager", "Terminal", "Calculator",
                "Text Editor", "Image Viewer", "Music Player",
                "Web Browser", "Paint", "Calendar",
                "System Monitor", "Weather", "Dictionary",
            };
            for (int i = 0; i < 12; i++) {
                gfx::text(s, 25, y, apps[i], 0xcdd6f4, 0);
                y += 22;
            }
            break;
        }
        case 3: {
            gfx::text(s, 15, y, "=== Terminal Commands ===", 0x89b4fa, 0);
            y += 35;
            const char* cmds[] = {
                "ls          - list files",
                "cd <dir>    - change dir",
                "pwd         - print path",
                "cat <file>  - show file",
                "mkdir <dir> - make dir",
                "rm <file>   - delete file",
                "echo <text> - print",
                "clear       - clear screen",
                "tree        - file tree",
                "help        - show help",
                "about       - about",
                "exit        - exit",
            };
            for (int i = 0; i < 12; i++) {
                gfx::text(s, 15, y, cmds[i], 0xa6e3a1, 0);
                y += 20;
            }
            break;
        }
    }
}

void app_help_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Help", x, y, 340, 400);
    if (!w) return;
    HelpState* st = new HelpState();
    st->w = w->content_w;
    st->h = w->content_h;
    st->page = 0;
    w->userdata = st;
    w->on_paint = help_paint;
    g_wm->raise(w);
}

} // namespace nefu
