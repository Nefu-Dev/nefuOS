// nefuOS ：9x9、10 ，，，double-click number spread
#include "apps.h"
#include "../gui/gfx.h"
#include "../platform.h"

namespace nefu {

struct MinerState {
    static const int N = 9, MINES = 10;
    bool mine[N][N];
    bool open[N][N];
    bool flag[N][N];
    int around[N][N];
    bool over;
    bool won;
    int opened;
    int flags;
    int cs;    // cell size
    int ox, oy;
    Window* win;
};

static void miner_place(MinerState* st, int skip_x, int skip_y) {
    int placed = 0;
    for (int attempt = 0; attempt < 500 && placed < MinerState::MINES; attempt++) {
        int x = (int)((platform_tick_ms() * 2654435761u + attempt * 131) >> 13) % MinerState::N;
        int y = (int)((platform_tick_ms() * 40503u + attempt * 89) >> 13) % MinerState::N;
        if (x == skip_x && y == skip_y) continue;
        if (st->mine[y][x]) continue;
        st->mine[y][x] = true;
        placed++;
    }
    for (int y = 0; y < MinerState::N; y++)
        for (int x = 0; x < MinerState::N; x++) {
            int c = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < MinerState::N && ny < MinerState::N && st->mine[ny][nx]) c++;
                }
            st->around[y][x] = c;
        }
}

static void miner_reveal(MinerState* st, int x, int y) {
    if (x < 0 || y < 0 || x >= MinerState::N || y >= MinerState::N) return;
    if (st->open[y][x] || st->flag[y][x]) return;
    st->open[y][x] = true;
    st->opened++;
    if (st->mine[y][x]) { st->over = true; return; }
    if (st->around[y][x] == 0) {
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                if (dx || dy) miner_reveal(st, x + dx, y + dy);
    }
    if (st->opened >= MinerState::N * MinerState::N - MinerState::MINES) st->won = true;
}

static void miner_reset(MinerState* st) {
    for (int y = 0; y < MinerState::N; y++)
        for (int x = 0; x < MinerState::N; x++) {
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

static void miner_paint(Window* w) {
    MinerState* st = (MinerState*)w->userdata;
    Surface& s = w->back;
    s.fill(0x00E8E8E3);
    st->cs = 30;
    st->ox = (s.width - st->cs * MinerState::N) / 2;
    st->oy = 34 + (s.height - 34 - st->cs * MinerState::N) / 2;
    for (int y = 0; y < MinerState::N; y++) {
        for (int x = 0; x < MinerState::N; x++) {
            int px = st->ox + x * st->cs, py = st->oy + y * st->cs;
            if (st->open[y][x]) {
                gfx::fillrect(s, px + 1, py + 1, st->cs - 2, st->cs - 2, 0x00F5F5F0);
                if (st->mine[y][x]) {
                    gfx::fillcircle(s, px + st->cs / 2, py + st->cs / 2, 6, 0x002E2E2E);
                } else if (st->around[y][x] > 0) {
                    char num[2] = { (char)('0' + st->around[y][x]), 0 };
                    uint32_t col = 0x001F6FB6;
                    if (st->around[y][x] == 1) col = 0x001F6FB6;
                    else if (st->around[y][x] == 2) col = 0x001E8448;
                    else if (st->around[y][x] == 3) col = 0x00D64545;
                    else if (st->around[y][x] == 4) col = 0x006A3DB5;
                    else col = 0x00876444;
                    gfx::text(s, px + 11, py + 7, num, col, 0x00F5F5F0);
                }
                gfx::rect(s, px, py, st->cs, st->cs, 0x00C9C8C2);
            } else {
                gfx::fillrect(s, px + 1, py + 1, st->cs - 2, st->cs - 2, 0x00C6CBD4);
                if (st->flag[y][x]) {
                    gfx::fillrect(s, px + 8, py + 6, 5, 12, color::RED);
                    gfx::fillrect(s, px + 13, py + 6, 5, 3, color::RED);
                    gfx::fillrect(s, px + 6, py + 18, 16, 3, 0x00555A63);
                }
                gfx::rect(s, px, py, st->cs, st->cs, 0x00FFFFFF);
                gfx::rect(s, px + 1, py + 1, st->cs - 2, st->cs - 2, 0x00888D96);
            }
        }
    }
    char buf[64];
    if (st->won) ksprintf(buf, sizeof(buf), "YOU WIN!  mines: %d", MinerState::MINES);
    else if (st->over) ksprintf(buf, sizeof(buf), "BOOM!  flags: %d/%d  (R restart)", st->flags, MinerState::MINES);
    else ksprintf(buf, sizeof(buf), "Mines: %d  Flags: %d", MinerState::MINES, st->flags);
    gfx::text(s, 10, 8, buf, st->over ? color::RED : color::TEXT, 0x00E8E8E3);
    if (st->over) gfx::text(s, 10, 22, "Right-click to flag, R to restart", color::TEXT2, 0x00E8E8E3);
}

static void miner_mouse(Window* w, int mx, int my, uint8_t buttons) {
    MinerState* st = (MinerState*)w->userdata;
    if (!buttons) return;
    int x = (mx - st->ox) / st->cs;
    int y = (my - st->oy) / st->cs;
    if (x < 0 || y < 0 || x >= MinerState::N || y >= MinerState::N) return;
    if (st->over || st->won) return;
    if ((buttons & 1) && !(buttons & 2)) {  // left
        if (st->flag[y][x]) return;
        if (st->opened == 0 && st->around[y][x] > 0) {
            // ，
        }
        miner_reveal(st, x, y);
    } else if ((buttons & 2) && !(buttons & 1)) { // right
        if (st->open[y][x]) return;
        st->flag[y][x] = !st->flag[y][x];
        st->flags += st->flag[y][x] ? 1 : -1;
    }
}

static void miner_key(Window* w, const KeyEvent* e) {
    MinerState* st = (MinerState*)w->userdata;
    if (!e->down) return;
    if (e->ascii == 'r' || e->ascii == 'R') miner_reset(st);
}

static void miner_close(Window* w) {
    if (w->userdata) delete (MinerState*)w->userdata;
    w->userdata = 0;
}

void miner_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Minesweeper", x, y, 330, 380);
    if (!w) return;
    MinerState* st = new MinerState();
    st->win = w;
    w->userdata = st;
    w->on_paint = miner_paint;
    w->on_mouse = miner_mouse;
    w->on_key = miner_key;
    w->on_close = miner_close;
    miner_reset(st);
}

} // namespace nefu
