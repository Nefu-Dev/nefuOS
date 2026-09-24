// life.cpp —— nefuOS 康威生命游戏（Game of Life）
//
// 玩法：
//   ←/→/↑/↓      移动光标
//   空格          切换光标处细胞
//   N             单步演化
//   R             开始/暂停自动演化
//   + / -         加快/减慢演化速度
//   G/B/O         放滑翔机 / 闪烁灯 / 轻量级飞船
//   P             放脉冲星（Pulsar，15x15）
//   X             放十连串（Pentadecathlon）
//   C             清空
//   V             切换网格线显示
//
// 规则：
//   - 活细胞周围 2~3 个活邻居 → 继续活
//   - 死细胞周围恰好 3 个活邻居 → 复活
//   - 其它情况死亡
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"

namespace nefu {

namespace {

const int GW = 48;
const int GH = 36;
const int CELL = 10;
const int PW = GW * CELL;
const int PH = GH * CELL + 28;

struct Life {
    uint8_t cur[GH][GW];
    uint8_t nxt[GH][GW];
    int cursor_x, cursor_y;
    bool running;
    bool show_grid;
    int generation;
    int live;
    int step_ms;          // 每步间隔
    uint32_t last_step;

    void reset() {
        for (int y = 0; y < GH; y++)
            for (int x = 0; x < GW; x++) cur[y][x] = 0;
        cursor_x = GW / 2;
        cursor_y = GH / 2;
        running = false;
        show_grid = false;
        generation = 0;
        live = 0;
        step_ms = 120;
        last_step = 0;
    }

    void toggle() { cur[cursor_y][cursor_x] ^= 1; }

    void stamp(const uint8_t* pattern, int pw, int ph) {
        for (int y = 0; y < ph; y++) {
            for (int x = 0; x < pw; x++) {
                int gx = cursor_x + x;
                int gy = cursor_y + y;
                if (gx < 0 || gx >= GW || gy < 0 || gy >= GH) continue;
                cur[gy][gx] = pattern[y * pw + x];
            }
        }
    }

    int count_neighbors(int x, int y) const {
        int n = 0;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                int nx = x + dx, ny = y + dy;
                if (nx < 0 || nx >= GW || ny < 0 || ny >= GH) continue;
                n += cur[ny][nx];
            }
        }
        return n;
    }

    void step() {
        live = 0;
        for (int y = 0; y < GH; y++) {
            for (int x = 0; x < GW; x++) {
                int n = count_neighbors(x, y);
                int alive = cur[y][x];
                if (alive) nxt[y][x] = (n == 2 || n == 3) ? 1 : 0;
                else       nxt[y][x] = (n == 3) ? 1 : 0;
                live += nxt[y][x];
            }
        }
        for (int y = 0; y < GH; y++)
            for (int x = 0; x < GW; x++) cur[y][x] = nxt[y][x];
        generation++;
    }
};

// 预设图案（1 = 活细胞）
const uint8_t PAT_GLIDER[9] = {
    0,1,0,
    0,0,1,
    1,1,1
};
const uint8_t PAT_BLINKER[3] = { 1,1,1 };
const uint8_t PAT_LWSS[20] = {
    0,1,0,0,1,
    1,0,0,0,0,
    1,0,0,0,1,
    1,1,1,1,0
};
// Pulsar：15x15
const uint8_t PAT_PULSAR[225] = {
    0,0,0,0,1,1,1,0,1,1,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,0,0,0,1,0,1,0,1,0,1,0,0,0,1,
    1,0,0,0,1,0,1,0,1,0,1,0,0,0,1,
    1,0,0,0,1,0,1,0,1,0,1,0,0,0,1,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,1,1,1,0,1,1,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,1,1,1,0,1,1,1,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,0,0,0,1,0,1,0,1,0,1,0,0,0,1,
    1,0,0,0,1,0,1,0,1,0,1,0,0,0,1,
    1,0,0,0,1,0,1,0,1,0,1,0,0,0,1,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,1,1,1,0,1,1,1,0,0,0,0
};
// Pentadecathlon：10x3
const uint8_t PAT_PENTA[30] = {
    0,1,0,0,1,1,0,0,1,0,
    1,0,1,1,0,0,1,1,0,1,
    0,1,0,0,1,1,0,0,1,0
};
// R-pentomino：3x3（演化上千代的小图案）
const uint8_t PAT_RPENT[9] = {
    0,1,1,
    1,1,0,
    0,1,0
};
// Acorn：7x3（需要数千代稳定）
const uint8_t PAT_ACORN[21] = {
    0,1,0,0,0,0,0,
    0,0,0,1,0,0,0,
    1,1,0,0,1,1,1
};
// Diehard：8x3（130 代后消亡）
const uint8_t PAT_DIEHARD[24] = {
    0,0,0,0,0,0,1,0,
    1,1,0,0,0,0,0,0,
    0,1,0,0,0,1,1,1
};
// Block：2x2 静态方块
const uint8_t PAT_BLOCK[4] = { 1,1, 1,1 };
// Toad：4x2 闪烁
const uint8_t PAT_TOAD[8] = {
    0,1,1,1,
    1,1,1,0
};
// Beehive：6x5 静态
const uint8_t PAT_BEEHIVE[30] = {
    0,0,1,1,0,0,
    0,1,0,0,1,0,
    0,0,1,1,0,0,
    0,0,0,0,0,0,
    0,0,0,0,0,0
};

static void life_paint(Window* w) {
    Life* g = (Life*)w->userdata;
    Surface& s = w->back;

    gfx::fillrect(s, 0, 0, PW, PH, 0x00080810);

    for (int y = 0; y < GH; y++) {
        for (int x = 0; x < GW; x++) {
            if (g->cur[y][x]) {
                gfx::fillrect(s, x * CELL + 1, y * CELL + 1,
                              CELL - 2, CELL - 2, 0x0080FF80);
            }
        }
    }

    if (g->show_grid) {
        for (int x = 0; x <= GW; x++)
            gfx::fillrect(s, x * CELL, 0, 1, GH * CELL, 0x00182030);
        for (int y = 0; y <= GH; y++)
            gfx::fillrect(s, 0, y * CELL, GW * CELL, 1, 0x00182030);
    }

    gfx::rect(s, g->cursor_x * CELL, g->cursor_y * CELL, CELL, CELL, 0x00FF8000);

    char buf[100];
    ksprintf(buf, sizeof(buf),
             "Gen: %d   Live: %d   %s   [%s] speed=%dms",
             g->generation, g->live, g->running ? "RUN" : "PAUSE",
             g->show_grid ? "grid" : "no-grid", g->step_ms);
    gfx::fillrect(s, 0, GH * CELL, PW, 28, 0x00181828);
    gfx::text(s, 6, GH * CELL + 6, buf, 0x00FFFFFF, 0x00181828);
}

static void life_tick(Window* w) {
    Life* g = (Life*)w->userdata;
    if (!g->running) return;
    uint32_t now = platform_tick_ms();
    if (now - g->last_step < (uint32_t)g->step_ms) return;
    g->last_step = now;
    g->step();
}

static void life_key(Window* w, const KeyEvent* e) {
    Life* g = (Life*)w->userdata;
    if (!e->down) return;
    switch (e->keycode) {
    case KEY_UP:    g->cursor_y--; break;
    case KEY_DOWN:  g->cursor_y++; break;
    case KEY_LEFT:  g->cursor_x--; break;
    case KEY_RIGHT: g->cursor_x++; break;
    default: break;
    }
    if (g->cursor_x < 0) g->cursor_x = 0;
    if (g->cursor_x >= GW) g->cursor_x = GW - 1;
    if (g->cursor_y < 0) g->cursor_y = 0;
    if (g->cursor_y >= GH) g->cursor_y = GH - 1;

    switch (e->ascii) {
    case ' ': g->toggle(); return;
    case 'n': case 'N': g->step(); return;
    case 'r': case 'R': g->running = !g->running; g->last_step = platform_tick_ms(); return;
    case 'c': case 'C': g->reset(); return;
    case 'g': case 'G': g->stamp(PAT_GLIDER, 3, 3); return;
    case 'b': case 'B': g->stamp(PAT_BLINKER, 3, 1); return;
    case 'o': case 'O': g->stamp(PAT_LWSS, 5, 4); return;
    case 'p': case 'P': g->stamp(PAT_PULSAR, 15, 15); return;
    case 'x': case 'X': g->stamp(PAT_PENTA, 10, 3); return;
    case 'e': case 'E': g->stamp(PAT_RPENT, 3, 3); return;
    case 'a': case 'A': g->stamp(PAT_ACORN, 7, 3); return;
    case 'd': case 'D': g->stamp(PAT_DIEHARD, 8, 3); return;
    case 'k': case 'K': g->stamp(PAT_BLOCK, 2, 2); return;
    case 't': case 'T': g->stamp(PAT_TOAD, 4, 2); return;
    case 'h': case 'H': g->stamp(PAT_BEEHIVE, 6, 5); return;
    case 'v': case 'V': g->show_grid = !g->show_grid; return;
    case '+': case '=': g->step_ms -= 20; if (g->step_ms < 20) g->step_ms = 20; return;
    case '-': case '_': g->step_ms += 20; if (g->step_ms > 500) g->step_ms = 500; return;
    }
}

static void life_close(Window* w) {
    if (w->userdata) delete (Life*)w->userdata;
    w->userdata = 0;
}

} // namespace

void life_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Life", x, y, PW, PH);
    if (!w) return;
    Life* g = new Life();
    g->reset();
    w->userdata = g;
    w->on_paint = life_paint;
    w->on_tick = life_tick;
    w->on_key = life_key;
    w->on_close = life_close;
    g_wm->raise(w);
}

} // namespace nefu
