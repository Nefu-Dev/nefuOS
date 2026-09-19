// nefuOS minesweeper - WM window (surface back + direct blit), click reveal, F flag, R restart
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"

namespace nefu {

struct MinerCore {
    static const int N = 9, MINES = 10;
    bool mine[N][N];
    bool open[N][N];
    bool flag[N][N];
    int around[N][N];
    bool over;
    bool won;
    int opened;
    int flags;
};

static void miner_place(MinerCore* st, int skip_x, int skip_y) {
    int placed = 0;
    for (int attempt = 0; attempt < 500 && placed < MinerCore::MINES; attempt++) {
        int x = (int)((platform_tick_ms() * 2654435761u + attempt * 131) >> 13) % MinerCore::N;
        int y = (int)((platform_tick_ms() * 40503u + attempt * 89) >> 13) % MinerCore::N;
        if (x == skip_x && y == skip_y) continue;
        if (st->mine[y][x]) continue;
        st->mine[y][x] = true;
        placed++;
    }
    for (int y = 0; y < MinerCore::N; y++)
        for (int x = 0; x < MinerCore::N; x++) {
            int c = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < MinerCore::N && ny < MinerCore::N && st->mine[ny][nx]) c++;
                }
            st->around[y][x] = c;
        }
}

static void miner_reveal(MinerCore* st, int x, int y) {
    if (x < 0 || y < 0 || x >= MinerCore::N || y >= MinerCore::N) return;
    if (st->open[y][x] || st->flag[y][x]) return;
    st->open[y][x] = true;
    st->opened++;
    if (st->mine[y][x]) { st->over = true; return; }
    if (st->around[y][x] == 0) {
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                if (dx || dy) miner_reveal(st, x + dx, y + dy);
    }
    if (st->opened >= MinerCore::N * MinerCore::N - MinerCore::MINES) st->won = true;
}

static void miner_reset(MinerCore* st) {
    for (int y = 0; y < MinerCore::N; y++)
        for (int x = 0; x < MinerCore::N; x++) {
            st->mine[y][x] = false;
            st->open[y][x] = false;
            st->flag[y][x] = false;
            st->around[y][x] = 0;
        }
    st->over = false;
    st->won = false;
    st->opened = 0;
    st->flags = 0;
    miner_place(st, -1, -1);
}

struct MinerLvState {
    int w, h;
    int ox, oy;
    MinerCore core;
};

static void miner_wm_draw(Window* w) {
    Surface& s = w->back;
    MinerLvState* st = (MinerLvState*)w->userdata;
    int ww = s.width, hh = s.height;
    s.fill(0x00E8E8E3);
    MinerCore* c = &st->core;
    int cs = 30;
    st->ox = (ww - cs * MinerCore::N) / 2;
    st->oy = 30 + (hh - 30 - cs * MinerCore::N) / 2;
    for (int y = 0; y < MinerCore::N; y++) {
        for (int x = 0; x < MinerCore::N; x++) {
            int px = st->ox + x * cs, py = st->oy + y * cs;
            if (c->open[y][x]) {
                gfx::fillrect(s, px + 1, py + 1, cs - 2, cs - 2, 0x00F5F5F0);
                if (c->mine[y][x]) {
                    gfx::fillcircle(s, px + cs / 2, py + cs / 2, 6, 0x002E2E2E);
                } else if (c->around[y][x] > 0) {
                    char num[2] = { (char)('0' + c->around[y][x]), 0 };
                    uint32_t col = 0x001F6FB6;
                    if (c->around[y][x] == 1) col = 0x001F6FB6;
                    else if (c->around[y][x] == 2) col = 0x001E8448;
                    else if (c->around[y][x] == 3) col = 0x00D64545;
                    else if (c->around[y][x] == 4) col = 0x006A3DB5;
                    else col = 0x00876444;
                    gfx::text(s, px + 11, py + 7, num, col, 0x00F5F5F0);
                }
                gfx::rect(s, px, py, cs, cs, 0x00C9C8C2);
            } else {
                gfx::fillrect(s, px + 1, py + 1, cs - 2, cs - 2, 0x00C6CBD4);
                if (c->flag[y][x]) {
                    gfx::fillrect(s, px + 8, py + 6, 5, 12, color::RED);
                    gfx::fillrect(s, px + 13, py + 6, 5, 3, color::RED);
                    gfx::fillrect(s, px + 6, py + 18, 16, 3, 0x00555A63);
                }
                gfx::rect(s, px, py, cs, cs, 0x00FFFFFF);
                gfx::rect(s, px + 1, py + 1, cs - 2, cs - 2, 0x00888D96);
            }
        }
    }
    char buf[64];
    if (c->won) ksprintf(buf, sizeof(buf), "YOU WIN!  mines: %d", MinerCore::MINES);
    else if (c->over) ksprintf(buf, sizeof(buf), "BOOM!  flags: %d/%d  (R restart)", c->flags, MinerCore::MINES);
    else ksprintf(buf, sizeof(buf), "Mines: %d  Flags: %d", MinerCore::MINES, c->flags);
    gfx::text(s, 10, 6, buf, c->over ? color::RED : color::TEXT, 0x00E8E8E3);
    if (c->over) gfx::text(s, 10, 20, "Press F to flag, R to restart", color::TEXT2, 0x00E8E8E3);
}

static void miner_wm_mouse(Window* w, int mx, int my, uint8_t buttons) {
    MinerLvState* st = (MinerLvState*)w->userdata;
    if (!buttons) return;
    int x = (mx - st->ox) / 30;
    int y = (my - st->oy) / 30;
    if (x < 0 || y < 0 || x >= MinerCore::N || y >= MinerCore::N) return;
    if (st->core.over || st->core.won) return;
    miner_reveal(&st->core, x, y);
}

static void miner_wm_key(Window* w, const KeyEvent* e) {
    MinerLvState* st = (MinerLvState*)w->userdata;
    if (!e->down) return;
    char k = e->ascii;
    if (k == 'r' || k == 'R') { miner_reset(&st->core); return; }
    if (k == 'f' || k == 'F') {
        MinerCore* c = &st->core;
        if (c->over || c->won) return;
        for (int y = 0; y < MinerCore::N && !c->over; y++)
            for (int x = 0; x < MinerCore::N; x++)
                if (!c->open[y][x]) { c->flag[y][x] = !c->flag[y][x]; c->flags += c->flag[y][x] ? 1 : -1; return; }
    }
}

void miner_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Minesweeper", x, y, 340, 420);
    if (!w) return;
    MinerLvState* st = new MinerLvState();
    st->w = w->content_w;
    st->h = w->content_h;
    w->userdata = st;
    w->on_paint = miner_wm_draw;
    w->on_mouse = miner_wm_mouse;
    w->on_key = miner_wm_key;
    miner_reset(&st->core);
    g_wm->raise(w);
}
} // namespace nefu
