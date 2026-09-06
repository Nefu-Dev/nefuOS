// nefuOS system info
#include "apps.h"
#include "../gui/gfx.h"
#include "../platform.h"

namespace nefu {

static void si_paint(Window* w) {
    (void)w;
    Surface& s = w->back;
    s.fill(color::WHITE);
    uint32_t used = 0, total = 0;
    platform_mem_stats(&used, &total);
    char buf[160];
    int y = 10;
    ksprintf(buf, sizeof(buf), "nefuOS v0.1.0");
    gfx::text(s, 10, y, buf, color::BLUE, color::WHITE);
    y += 24;
    ksprintf(buf, sizeof(buf), "Backend    : %s", platform_name());
    gfx::text(s, 10, y, buf, color::TEXT, color::WHITE);
    y += 20;
    Screen* sc = platform_screen();
    ksprintf(buf, sizeof(buf), "Resolution : %dx%d (%d bpp)", sc->width, sc->height, 32);
    gfx::text(s, 10, y, buf, color::TEXT, color::WHITE);
    y += 20;
    ksprintf(buf, sizeof(buf), "Uptime     : %u s", nefuos_uptime_ms() / 1000);
    gfx::text(s, 10, y, buf, color::TEXT, color::WHITE);
    y += 20;
    ksprintf(buf, sizeof(buf), "Memory     : %u / %u KB", used / 1024, total / 1024);
    gfx::text(s, 10, y, buf, color::TEXT, color::WHITE);
    y += 20;
    ksprintf(buf, sizeof(buf), "VFS        : %d nodes, %u bytes", g_vfs->node_count(), g_vfs->total_bytes());
    gfx::text(s, 10, y, buf, color::TEXT, color::WHITE);
    y += 24;
    gfx::text(s, 10, y, "Built with C++ / C, dual backend.", color::TEXT2, color::WHITE);
    y += 20;
    gfx::text(s, 10, y, "Host: nefuOS.exe   Bare: nefuOS.iso", color::TEXT2, color::WHITE);
    y += 20;
    gfx::text(s, 10, y, "Try Terminal: ls / tree / cat", color::BLUE_LT, color::WHITE);
}

static void si_close(Window* w) {
    w->userdata = 0;
}

void sysinfo_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("System Info", x, y, 400, 260);
    if (!w) return;
    w->userdata = (void*)1; // placeholder
    w->on_paint = si_paint;
    w->on_close = si_close;
}

} // namespace nefu
