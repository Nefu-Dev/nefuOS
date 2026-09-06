// nefuOS system monitor：CPU / memory / disk live curve +
#include "apps.h"
#include "../gui/gfx.h"
#include "../platform.h"

namespace nefu {

static uint32_t mo_lcg = 424242;
static uint32_t morand() {
    mo_lcg = mo_lcg * 1103515245u + 12345u;
    return (mo_lcg >> 16) & 0x7FFF;
}

struct MonitorState {
    int cpu[80], mem[80], disk[80];
    int head;                 // latest data index（ring）
    uint32_t last_push;
    Window* win;
};

static const int HIST = 80;

static void mo_push(MonitorState* st) {
    // CPU：fake load（fluctuation + ，integer）
    static int phase = 0;
    phase++;
    int base = 32 + (20 - (phase % 40));   // 52 .. 13
    if (base < 8) base = 8;
    int c = base + (int)(morand() % 25);
    if (c > 95) c = 95;
    if (c < 2) c = 2;
    st->cpu[st->head] = c;
    // memory：real
    uint32_t used = 0, total = 0;
    platform_mem_stats(&used, &total);
    int m = total > 0 ? (int)((uint64_t)used * 100 / total) : 0;
    if (m > 100) m = 100;
    st->mem[st->head] = m;
    // disk：VFS usage（by 64MB virtual capacity estimate）
    uint32_t bytes = g_vfs->total_bytes();
    int d = (int)((uint64_t)bytes * 100 / (64u * 1024 * 1024));
    if (d > 100) d = 100;
    st->disk[st->head] = d;
    st->head = (st->head + 1) % HIST;
}

static void mo_draw_curve(Surface& s, int x, int y, int w, int h,
                          const int* data, int head, uint32_t color_, const char* label) {
    gfx::fillrect(s, x, y, w, h, 0x00F5F5F0);
    gfx::rect(s, x, y, w, h, color::BORDER);
    gfx::text(s, x + 4, y + 2, label, color::TEXT2, 0x00F5F5F0);
    // grid
    for (int i = 1; i < 4; i++) {
        gfx::hline(s, x + 1, x + w - 2, y + h * i / 4, 0x00E3E2DC);
    }
    int lastx = -1, lasty = -1;
    for (int i = 0; i < HIST; i++) {
        int idx = (head + i) % HIST;   // old -> new
        int v = data[idx];
        int px = x + 2 + i * (w - 4) / (HIST - 1);
        int py = y + h - 4 - v * (h - 12) / 100;
        if (lastx >= 0) gfx::line(s, lastx, lasty, px, py, color_);
        lastx = px;
        lasty = py;
    }
}

static void mo_paint(Window* w) {
    MonitorState* st = (MonitorState*)w->userdata;
    Surface& s = w->back;
    s.fill(color::WHITE);
    int W = s.width, H = s.height;

    uint32_t now = platform_tick_ms();
    if (now - st->last_push >= 400) {
        mo_push(st);
        st->last_push = now;
    }

    gfx::text(s, 10, 8, "System Monitor", color::BLUE, color::WHITE);
    gfx::hline(s, 8, W - 8, 26, color::BORDER);

    // curve area（left 2/3）
    int cw = W * 2 / 3 - 20;
    int ch = (H - 40) / 3;
    mo_draw_curve(s, 10, 34, cw, ch - 6, st->cpu, st->head, color::RED, "CPU %");
    mo_draw_curve(s, 10, 34 + ch, cw, ch - 6, st->mem, st->head, color::BLUE_LT, "Memory %");
    mo_draw_curve(s, 10, 34 + ch * 2, cw, ch - 6, st->disk, st->head, color::GREEN, "Disk %");

    // （right 1/3）
    int px = 10 + cw + 10;
    int pw = W - px - 10;
    gfx::fillrect(s, px, 34, pw, H - 44, 0x00F5F5F0);
    gfx::rect(s, px, 34, pw, H - 44, color::BORDER);
    gfx::text(s, px + 8, 40, "Live values", color::TEXT2, 0x00F5F5F0);

    uint32_t used = 0, total = 0;
    platform_mem_stats(&used, &total);
    char buf[96];
    int cpu_now = st->cpu[(st->head - 1 + HIST) % HIST];
    int mem_now = st->mem[(st->head - 1 + HIST) % HIST];
    int disk_now = st->disk[(st->head - 1 + HIST) % HIST];
    int y = 62;
    ksprintf(buf, sizeof(buf), "CPU    %3d %%", cpu_now);
    gfx::text(s, px + 8, y, buf, color::TEXT, 0x00F5F5F0); y += 22;
    ksprintf(buf, sizeof(buf), "RAM    %3d %%  (%u/%u MB)", mem_now,
             (unsigned)(used / 1048576), (unsigned)(total / 1048576));
    gfx::text(s, px + 8, y, buf, color::TEXT, 0x00F5F5F0); y += 22;
    ksprintf(buf, sizeof(buf), "DISK   %3d %%  (%u KB)", disk_now,
             (unsigned)(g_vfs->total_bytes() / 1024));
    gfx::text(s, px + 8, y, buf, color::TEXT, 0x00F5F5F0); y += 22;
    ksprintf(buf, sizeof(buf), "Nodes  %d", g_vfs->node_count());
    gfx::text(s, px + 8, y, buf, color::TEXT, 0x00F5F5F0); y += 22;
    ksprintf(buf, sizeof(buf), "Uptime %u s", nefuos_uptime_ms() / 1000);
    gfx::text(s, px + 8, y, buf, color::TEXT, 0x00F5F5F0); y += 30;
    gfx::text(s, px + 8, y, "Refresh: 400ms", color::TEXT2, 0x00F5F5F0); y += 18;
    gfx::text(s, px + 8, y, "CPU curve is simulated;", color::TEXT2, 0x00F5F5F0); y += 18;
    gfx::text(s, px + 8, y, "RAM/DISK are real values.", color::TEXT2, 0x00F5F5F0);
}

static void mo_close(Window* w) {
    if (w->userdata) delete (MonitorState*)w->userdata;
    w->userdata = 0;
}

void monitor_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("System Monitor", x, y, 560, 400);
    if (!w) return;
    MonitorState* st = new MonitorState();
    st->win = w;
    st->head = 0;
    st->last_push = 0;
    for (int i = 0; i < HIST; i++) { st->cpu[i] = 0; st->mem[i] = 0; st->disk[i] = 0; }

    for (int i = 0; i < HIST; i++) mo_push(st);
    w->userdata = st;
    w->on_paint = mo_paint;
    w->on_close = mo_close;
}

} // namespace nefu
