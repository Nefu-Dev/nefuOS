// minesweeper2.cpp —— nefuOS 高级扫雷
//
// 玩法：
//   ←/→/↑/↓      移动光标
//   空格          挖开格子（首次点击自动保证不踩雷）
//   F             插旗/拔旗
//   C             在已展开且数字周围旗数==数字时，自动挖开其余邻格（chording）
//   1/2/3         切换难度：初级 9x9/10、中级 16x16/40、高级 24x16/99
//   R             按当前难度重开
//
// 规则：
//   - 数字表示周围 8 格的雷数
//   - 挖开 0 会自动 flood-fill 展开
//   - 全部安全格挖开即胜；踩到雷即败
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "games2_util.h"

namespace nefu {

namespace {

using namespace nefu::games2;

inline int iabs(int v) { return v < 0 ? -v : v; }

const int MAX_W = 24;
const int MAX_H = 18;
const int CELL = 22;

enum CellState { CS_HIDDEN = 0, CS_OPEN, CS_FLAG };

struct Difficulty {
    int w, h, mines;
    const char* name;
};

const Difficulty DIFFS[3] = {
    { 9,  9,  10, "Beginner" },
    { 16, 16, 40, "Intermediate" },
    { 24, 16, 99, "Expert" }
};

struct MineGame {
    uint8_t grid[MAX_H][MAX_W];   // 0..8 = 周围雷数，9 = 雷
    uint8_t state[MAX_H][MAX_W];
    int w, h, mines;
    int cursor_x, cursor_y;
    bool started;
    bool over;
    bool win;
    int opened_count;
    int flags_placed;
    uint32_t start_time;
    Rng rng;

    void set_difficulty(int d) {
        w = DIFFS[d].w;
        h = DIFFS[d].h;
        mines = DIFFS[d].mines;
    }

    void reset() {
        for (int y = 0; y < MAX_H; y++)
            for (int x = 0; x < MAX_W; x++) {
                grid[y][x] = 0;
                state[y][x] = CS_HIDDEN;
            }
        cursor_x = w / 2;
        cursor_y = h / 2;
        started = false;
        over = false;
        win = false;
        opened_count = 0;
        flags_placed = 0;
        start_time = 0;
        rng.seed((uint32_t)(platform_tick_ms() & 0xFFFFu));
    }

    void place_mines(int sx, int sy) {
        int placed = 0;
        while (placed < mines) {
            int x = rng.range(0, w - 1);
            int y = rng.range(0, h - 1);
            if (iabs(x - sx) <= 1 && iabs(y - sy) <= 1) continue;
            if (grid[y][x] == 9) continue;
            grid[y][x] = 9;
            placed++;
        }
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                if (grid[y][x] == 9) continue;
                int n = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = x + dx, ny = y + dy;
                        if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                        if (grid[ny][nx] == 9) n++;
                    }
                grid[y][x] = (uint8_t)n;
            }
        }
        started = true;
        start_time = platform_tick_ms();
    }

    void open(int x, int y) {
        if (x < 0 || x >= w || y < 0 || y >= h) return;
        if (state[y][x] != CS_HIDDEN) return;
        state[y][x] = CS_OPEN;
        opened_count++;
        if (grid[y][x] == 0) {
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    open(x + dx, y + dy);
                }
        }
    }

    void reveal_at_cursor() {
        if (over) return;
        int x = cursor_x, y = cursor_y;
        if (state[y][x] == CS_FLAG) return;
        if (!started) place_mines(x, y);
        if (grid[y][x] == 9) {
            // 踩雷：翻开所有雷
            for (int yy = 0; yy < h; yy++)
                for (int xx = 0; xx < w; xx++)
                    if (grid[yy][xx] == 9) state[yy][xx] = CS_OPEN;
            state[y][x] = CS_OPEN;
            over = true;
            return;
        }
        open(x, y);
        if (opened_count == w * h - mines) {
            win = true;
            over = true;
        }
    }

    // chording：数字格周围旗数==数字时，挖开其它邻格
    void chord_at_cursor() {
        if (over) return;
        int x = cursor_x, y = cursor_y;
        if (state[y][x] != CS_OPEN || grid[y][x] == 0) return;
        int flags = 0;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                int nx = x + dx, ny = y + dy;
                if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                if (state[ny][nx] == CS_FLAG) flags++;
            }
        if (flags != grid[y][x]) return;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                int nx = x + dx, ny = y + dy;
                if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                if (state[ny][nx] == CS_HIDDEN) {
                    if (grid[ny][nx] == 9) {
                        for (int yy = 0; yy < h; yy++)
                            for (int xx = 0; xx < w; xx++)
                                if (grid[yy][xx] == 9) state[yy][xx] = CS_OPEN;
                        over = true;
                        return;
                    }
                    open(nx, ny);
                }
            }
        if (opened_count == w * h - mines) {
            win = true;
            over = true;
        }
    }

    void toggle_flag_at_cursor() {
        if (over) return;
        int x = cursor_x, y = cursor_y;
        if (state[y][x] == CS_HIDDEN) {
            state[y][x] = CS_FLAG;
            flags_placed++;
        } else if (state[y][x] == CS_FLAG) {
            state[y][x] = CS_HIDDEN;
            flags_placed--;
        }
    }

    void move_cursor(int dx, int dy) {
        cursor_x += dx;
        cursor_y += dy;
        if (cursor_x < 0) cursor_x = 0;
        if (cursor_x >= w) cursor_x = w - 1;
        if (cursor_y < 0) cursor_y = 0;
        if (cursor_y >= h) cursor_y = h - 1;
    }
};

static void ms_paint(Window* w) {
    MineGame* g = (MineGame*)w->userdata;
    Surface& s = w->back;

    int pw = g->w * CELL;
    int ph = g->h * CELL + 40;
    gfx::fillrect(s, 0, 0, pw, ph, 0x00C0C0C0);

    for (int y = 0; y < g->h; y++) {
        for (int x = 0; x < g->w; x++) {
            int px = x * CELL, py = y * CELL;
            uint32_t bg = 0x00C0C0C0;
            if (g->state[y][x] == CS_OPEN) bg = 0x00E8E8E8;
            gfx::fillrect(s, px + 1, py + 1, CELL - 2, CELL - 2, bg);

            if (g->state[y][x] == CS_FLAG) {
                gfx::fillrect(s, px + CELL / 2 - 2, py + 5, 4, CELL - 10, 0x00FF2020);
            } else if (g->state[y][x] == CS_OPEN) {
                if (g->grid[y][x] == 9) {
                    gfx::fillcircle(s, px + CELL / 2, py + CELL / 2, 6, 0x00000000);
                } else if (g->grid[y][x] > 0) {
                    char buf[4];
                    buf[0] = '0' + g->grid[y][x];
                    buf[1] = 0;
                    static const uint32_t NUM_COLORS[9] = {
                        0, 0x000000FF, 0x00008000, 0x00FF0000, 0x00000080,
                        0x00800000, 0x00008080, 0x00000000, 0x00808080
                    };
                    gfx::text(s, px + 6, py + 4, buf, NUM_COLORS[g->grid[y][x]], 0x00E8E8E8);
                }
            }
        }
    }

    gfx::rect(s, g->cursor_x * CELL, g->cursor_y * CELL, CELL, CELL, 0x00FF8000);

    char buf[80];
    int sec = g->started ? (int)((platform_tick_ms() - g->start_time) / 1000) : 0;
    ksprintf(buf, sizeof(buf), "Mines: %d/%d   Time: %ds",
            g->flags_placed, g->mines, sec);
    gfx::fillrect(s, 0, g->h * CELL, pw, 40, 0x00202020);
    gfx::text(s, 8, g->h * CELL + 12, buf, 0x00FFFFFF, 0x00202020);
    if (g->over) {
        const char* msg = g->win ? "YOU WIN! (R=restart)" : "BOOM! (R=restart)";
        gfx::text(s, 220, g->h * CELL + 12, msg,
                  g->win ? 0x0040FF40 : 0x00FF4040, 0x00202020);
    }
}

static void ms_tick(Window* w) { (void)w; }

static void ms_key(Window* w, const KeyEvent* e) {
    MineGame* g = (MineGame*)w->userdata;
    if (!e->down) return;
    switch (e->keycode) {
    case KEY_UP:    g->move_cursor(0, -1); return;
    case KEY_DOWN:  g->move_cursor(0, 1);  return;
    case KEY_LEFT:  g->move_cursor(-1, 0); return;
    case KEY_RIGHT: g->move_cursor(1, 0);  return;
    default: break;
    }
    if (e->ascii == ' ' || e->keycode == KEY_ENTER) { g->reveal_at_cursor(); return; }
    if (e->ascii == 'f' || e->ascii == 'F') { g->toggle_flag_at_cursor(); return; }
    if (e->ascii == 'c' || e->ascii == 'C') { g->chord_at_cursor(); return; }
    if (e->ascii == 'r' || e->ascii == 'R') { g->reset(); return; }
    if (e->ascii == '1') { g->set_difficulty(0); g->reset(); return; }
    if (e->ascii == '2') { g->set_difficulty(1); g->reset(); return; }
    if (e->ascii == '3') { g->set_difficulty(2); g->reset(); return; }
}

static void ms_close(Window* w) {
    if (w->userdata) delete (MineGame*)w->userdata;
    w->userdata = 0;
}

} // namespace

void minesweeper2_launch() {
    int x, y;
    cascade_pos(&x, &y);
    MineGame* g = new MineGame();
    g->set_difficulty(1);   // 默认中级
    g->reset();
    // 窗口尺寸按难度算
    int pw = g->w * CELL;
    int ph = g->h * CELL + 40;
    Window* w = g_wm->create_window("Minesweeper Pro", x, y, pw, ph);
    if (!w) return;
    w->userdata = g;
    w->on_paint = ms_paint;
    w->on_tick = ms_tick;
    w->on_key = ms_key;
    w->on_close = ms_close;
    g_wm->raise(w);
}

} // namespace nefu
