// nefuOS Sudoku — generate, solve, play
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../gui/widgets.h"
#include "../platform.h"

namespace nefu {

namespace {

const int SZ = 9;

struct Sudoku {
    int w, h;
    int board[SZ][SZ];      // current user view (0 = empty)
    int solved[SZ][SZ];     // full solution
    bool fixed[SZ][SZ];     // given cells (locked)
    int sel_x, sel_y;
    bool over;
    int elapsed;
};

// ---- solver (backtracking) ----
bool sud_ok(const int b[SZ][SZ], int r, int c, int v) {
    for (int i = 0; i < SZ; i++) {
        if (b[r][i] == v || b[i][c] == v) return false;
    }
    int br = r / 3 * 3, bc = c / 3 * 3;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            if (b[br + i][bc + j] == v) return false;
    return true;
}

bool sud_solve(int b[SZ][SZ], int pos) {
    if (pos >= SZ * SZ) return true;
    int r = pos / SZ, c = pos % SZ;
    if (b[r][c]) return sud_solve(b, pos + 1);
    for (int v = 1; v <= 9; v++) {
        if (sud_ok(b, r, c, v)) {
            b[r][c] = v;
            if (sud_solve(b, pos + 1)) return true;
            b[r][c] = 0;
        }
    }
    return false;
}

void sud_generate(Sudoku& g) {
    // clear
    for (int r = 0; r < SZ; r++) for (int c = 0; c < SZ; c++) g.board[r][c] = g.solved[r][c] = 0;
    // fill diagonal 3x3 boxes with shuffled digits (guarantees solvable base)
    uint32_t seed = platform_tick_ms() ^ 0x5A5A5A5A;
    for (int box = 0; box < 3; box++) {
        int digits[9] = {1,2,3,4,5,6,7,8,9};
        // shuffle
        for (int i = 8; i > 0; i--) {
            seed = seed * 1103515245 + 12345;
            int j = (int)((seed >> 16) % (uint32_t)(i + 1));
            int t = digits[i]; digits[i] = digits[j]; digits[j] = t;
        }
        int n = 0;
        for (int r = box * 3; r < box * 3 + 3; r++)
            for (int c = box * 3; c < box * 3 + 3; c++)
                g.solved[r][c] = digits[n++];
    }
    // solve the rest
    if (!sud_solve(g.solved, 0)) {
        // fallback: fixed puzzle
        static const int FIXED[SZ][SZ] = {
            {5,3,0,0,7,0,0,0,0},{6,0,0,1,9,5,0,0,0},{0,9,8,0,0,0,0,6,0},
            {8,0,0,0,6,0,0,0,3},{4,0,0,8,0,3,0,0,1},{7,0,0,0,2,0,0,0,6},
            {0,6,0,0,0,0,2,8,0},{0,0,0,4,1,9,0,0,5},{0,0,0,0,8,0,0,7,9}
        };
        for (int r = 0; r < SZ; r++) for (int c = 0; c < SZ; c++) g.solved[r][c] = FIXED[r][c];
        sud_solve(g.solved, 0);
    }
    // dig holes
    int holes = 45;
    for (int h = 0; h < holes; h++) {
        seed = seed * 1103515245 + 12345;
        int r = (int)((seed >> 16) % 9);
        seed = seed * 1103515245 + 12345;
        int c = (int)((seed >> 16) % 9);
        if (g.board[r][c] != 0) { h--; continue; }
        // try removing: check unique solution is expensive; just remove
        // (generator from solved puzzle always keeps at least one solution)
        g.board[r][c] = 0;
    }
    // fill view board with solution minus holes
    for (int r = 0; r < SZ; r++) {
        for (int c = 0; c < SZ; c++) {
            bool given = (g.board[r][c] == 0); // hole marker (reuse)
            g.board[r][c] = given ? 0 : g.solved[r][c];
            g.fixed[r][c] = !given;
        }
    }
    g.sel_x = 0; g.sel_y = 0;
    g.over = false;
    g.elapsed = 0;
}

void sud_check_win(Sudoku& g) {
    bool full = true;
    for (int r = 0; r < SZ && full; r++)
        for (int c = 0; c < SZ && full; c++)
            if (g.board[r][c] != g.solved[r][c]) full = false;
    if (full) g.over = true;
}

void sud_paint(Window* win) {
    Sudoku* g = (Sudoku*)win->userdata;
    Surface& s = win->back;
    s.fill(0x00FDF6E3);
    gfx::text_scale(s, 12, 8, "Sudoku", 0x00446871, 0x00FDF6E3, 2);
    char buf[64];
    ksprintf(buf, sizeof(buf), "Time: %ds", g->elapsed);
    gfx::text(s, 12, 40, buf, 0x00446871, 0x00FDF6E3);
    int cell = (g->w - 40) / 9;
    if (cell < 26) cell = 26;
    int ox = (g->w - cell * 9) / 2;
    int oy = 62;
    // cells
    for (int r = 0; r < SZ; r++) {
        for (int c = 0; c < SZ; c++) {
            int x = ox + c * cell, y = oy + r * cell;
            uint32_t bg = 0x00FFFFFF;
            if (g->fixed[r][c]) bg = 0x00EDE8D6;
            if (r == g->sel_y && c == g->sel_x) bg = 0x00C8E0F0;
            gfx::fillrect(s, x, y, cell, cell, bg);
            if (g->board[r][c]) {
                char num[4];
                ksprintf(num, sizeof(num), "%d", g->board[r][c]);
                gfx::text(s, x + cell / 2 - 4, y + cell / 2 - 7, num,
                          g->fixed[r][c] ? 0x00444444 : 0x002255CC, bg);
            }
        }
    }
    // grid lines (thicker for 3x3)
    for (int i = 0; i <= 9; i++) {
        int w = (i % 3 == 0) ? 2 : 1;
        gfx::vline(s, ox + i * cell - (w == 2 ? 1 : 0), oy, oy + cell * 9, 0x00446871);
        gfx::hline(s, ox, ox + cell * 9, oy + i * cell - (w == 2 ? 1 : 0), 0x00446871);
    }
    if (g->over) {
        gfx::fillrect(s, 0, g->h - 32, g->w, 32, 0x00008800);
        gfx::text(s, 8, g->h - 26, "SOLVED!  R = new game", color::WHITE, 0x00008800);
    }
    gfx::text(s, 8, g->h - 14, "Click cell, type 1-9   R = new", 0x00888888, 0x00FDF6E3);
}

void sud_mouse(Window* w, int mx, int my, uint8_t buttons) {
    Sudoku* g = (Sudoku*)w->userdata;
    if (!buttons) return;
    int cell = (g->w - 40) / 9;
    if (cell < 26) cell = 26;
    int ox = (g->w - cell * 9) / 2;
    int oy = 62;
    if (mx >= ox && mx < ox + cell * 9 && my >= oy && my < oy + cell * 9) {
        g->sel_x = (mx - ox) / cell;
        g->sel_y = (my - oy) / cell;
    }
}

void sud_key(Window* w, const KeyEvent* e) {
    Sudoku* g = (Sudoku*)w->userdata;
    if (!e->down) return;
    if (e->ascii == 'r' || e->ascii == 'R') { sud_generate(*g); return; }
    if (e->ascii >= '1' && e->ascii <= '9') {
        int v = e->ascii - '0';
        if (!g->fixed[g->sel_y][g->sel_x]) {
            g->board[g->sel_y][g->sel_x] = v;
            sud_check_win(*g);
        }
        return;
    }
    if (e->ascii == '0' || e->ascii == ' ') {
        if (!g->fixed[g->sel_y][g->sel_x]) g->board[g->sel_y][g->sel_x] = 0;
        return;
    }
    switch (e->keycode) {
    case KEY_LEFT: if (g->sel_x > 0) g->sel_x--; break;
    case KEY_RIGHT: if (g->sel_x < 8) g->sel_x++; break;
    case KEY_UP: if (g->sel_y > 0) g->sel_y--; break;
    case KEY_DOWN: if (g->sel_y < 8) g->sel_y++; break;
    default: break;
    }
}

void sud_tick(Window* w) {
    Sudoku* g = (Sudoku*)w->userdata;
    if (!g->over) {
        static uint32_t last = 0;
        uint32_t now = platform_tick_ms();
        if (now - last >= 1000) { g->elapsed++; last = now; }
    }
}

void sud_close(Window* w) {
    if (w->userdata) delete (Sudoku*)w->userdata;
    w->userdata = 0;
}

} // namespace

void sudoku_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Sudoku", x, y, 340, 420);
    if (!w) return;
    Sudoku* g = new Sudoku();
    g->w = w->content_w;
    g->h = w->content_h;
    sud_generate(*g);
    w->userdata = g;
    w->on_paint = sud_paint;
    w->on_mouse = sud_mouse;
    w->on_key = sud_key;
    w->on_tick = sud_tick;
    w->on_close = sud_close;
    g_wm->raise(w);
}

} // namespace nefu
