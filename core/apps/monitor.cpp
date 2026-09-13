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
    int proc_scroll;          // process list scroll
    uint8_t last_buttons;
    int last_click_y;         // window-relative y of last click (proc list)
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

    // ---- process list: every open window is a process ----
    int py0 = 34 + ch * 3 + 6;
    if (py0 + 20 < H - 8) {
        gfx::hline(s, 8, W - 8, py0 - 3, color::BORDER);
        gfx::text(s, 10, py0, "Processes (open windows)", color::BLUE, color::WHITE);
        int ly = py0 + 16;
        List<Window*>& ws = g_wm->windows();
        int vis = (H - ly - 8) / 16;
        if (vis < 1) vis = 1;
        for (int i = st->proc_scroll; i < ws.size() && i < st->proc_scroll + vis; i++) {
            Window* p = ws[i];
            if (p->closed) continue;
            bool foc = (g_wm->focus() == p);
            uint32_t bg = foc ? 0x00E3EEF8 : color::WHITE;
            gfx::fillrect(s, 10, ly, W - 90, 15, bg);
            char line[120];
            uint32_t est = (uint32_t)((uint64_t)(p->back.width * p->back.height) * 4);
            ksprintf(line, sizeof(line), "%s  [%u KB]",
                     p->title.c_str(), (unsigned)(est / 1024));
            gfx::text(s, 14, ly + 1, line, foc ? color::BLUE : color::TEXT, bg);
            // kill button
            gfx::fillrect(s, W - 76, ly + 1, 62, 13, 0x00E84C4C);
            gfx::text(s, W - 70, ly + 2, "End", color::WHITE, 0x00E84C4C);
            ly += 16;
        }
        gfx::text(s, 10, ly + 2, "Click End to close a process.", color::TEXT2, color::WHITE);
    }
}

static void mo_mouse(Window* w, int mx, int my, uint8_t buttons) {
    MonitorState* st = (MonitorState*)w->userdata;
    bool pressed = buttons && !st->last_buttons;
    st->last_buttons = buttons;
    if (!pressed) return;
    int W = w->back.width, H = w->back.height;
    int ch = (H - 40) / 3;
    int py0 = 34 + ch * 3 + 6;
    if (my < py0 + 16) return;
    int row = (my - py0 - 16) / 16;
    List<Window*>& ws = g_wm->windows();
    // find nth visible non-closed window
    int visible = 0;
    for (int i = 0; i < ws.size(); i++) {
        if (ws[i]->closed) continue;
        if (visible == row + st->proc_scroll) {
            // End button hit box
            if (mx >= W - 76 && mx <= W - 14) {
                g_wm->close_window(ws[i]);
                st->last_click_y = my;
            }
            return;
        }
        visible++;
    }
}

static void mo_scroll(Window* w, int delta) {
    MonitorState* st = (MonitorState*)w->userdata;
    st->proc_scroll += delta > 0 ? -1 : 1;
    if (st->proc_scroll < 0) st->proc_scroll = 0;
    (void)w;
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
    st->proc_scroll = 0;
    st->last_buttons = 0;
    st->last_click_y = 0;
    for (int i = 0; i < HIST; i++) { st->cpu[i] = 0; st->mem[i] = 0; st->disk[i] = 0; }

    for (int i = 0; i < HIST; i++) mo_push(st);
    w->userdata = st;
    w->on_paint = mo_paint;
    w->on_mouse = mo_mouse;
    w->on_scroll = mo_scroll;
    w->on_close = mo_close;
}

} // namespace nefu
