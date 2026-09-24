// nefuOS 2048 — sliding tile puzzle
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"

namespace nefu {

namespace {

const uint32_t TILE_COLORS[16] = {
    0x00EEE4DA, 0x00EDE0C8, 0x00F2B179, 0x00F59563, 0x00F67C5F, 0x00F65E3B,
    0x00EDCF72, 0x00EDCC61, 0x00EDC850, 0x00EDC53F, 0x00EDC22E, 0x00B8860B,
    0x00A0522D, 0x008B4513, 0x006B3410, 0x00500000
};

struct G2048 {
    int w, h;
    uint32_t board[4][4];
    int score;
    bool over, won;
    uint8_t last_keys;
};

void g_clear(G2048& g) {
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) g.board[y][x] = 0;
    g.score = 0; g.over = false; g.won = false;
}

void g_add_tile(G2048& g) {
    // collect empty cells
    int empty[16];
    int n = 0;
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) if (!g.board[y][x]) empty[n++] = y * 4 + x;
    if (n == 0) return;
    uint32_t seed = platform_tick_ms() + (uint32_t)n * 7919;
    int idx = empty[seed % (uint32_t)n];
    g.board[idx / 4][idx % 4] = (seed % 10 == 0) ? 4 : 2;
}

void g_spawn(G2048& g) {
    g_clear(g);
    g_add_tile(g);
    g_add_tile(g);
}

// returns true if board changed
bool g_move(G2048& g, int dx, int dy) {
    bool changed = false;
    for (int i = 0; i < 4; i++) {
        // build line (cells in movement direction)
        uint32_t line[4];
        for (int k = 0; k < 4; k++) {
            int x = (dx > 0) ? (3 - k) : k;
            int y = (dy > 0) ? (3 - k) : k;
            if (dx) y = i;
            if (dy) x = i;
            line[k] = g.board[y][x];
        }
        // move & merge (single pass, each tile merges at most once)
        uint32_t out[4] = {0, 0, 0, 0};
        int o = 0;
        bool merged[4] = {false, false, false, false};
        for (int k = 0; k < 4; k++) {
            if (!line[k]) continue;
            if (o > 0 && out[o - 1] == line[k] && !merged[o - 1]) {
                out[o - 1] *= 2;
                g.score += (int)out[o - 1];
                merged[o - 1] = true;
                if (out[o - 1] == 2048) g.won = true;
            } else {
                out[o++] = line[k];
            }
        }
        // write back
        for (int k = 0; k < 4; k++) {
            int x = (dx > 0) ? (3 - k) : k;
            int y = (dy > 0) ? (3 - k) : k;
            if (dx) y = i;
            if (dy) x = i;
            if (g.board[y][x] != out[k]) changed = true;
            g.board[y][x] = out[k];
        }
    }
    return changed;
}

bool g_can_move(const G2048& g) {
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (!g.board[y][x]) return true;
            if (x < 3 && g.board[y][x] == g.board[y][x + 1]) return true;
            if (y < 3 && g.board[y][x] == g.board[y + 1][x]) return true;
        }
    }
    return false;
}

void g_paint(Window* win) {
    G2048* g = (G2048*)win->userdata;
    Surface& s = win->back;
    s.fill(0x00FAF8EF);
    gfx::text_scale(s, 12, 10, "2048", 0x00776756, 0x00FAF8EF, 2);
    char buf[64];
    ksprintf(buf, sizeof(buf), "Score: %d", g->score);
    gfx::text(s, 12, 48, buf, 0x00776756, 0x00FAF8EF);
    int cell = (g->w - 30) / 4;
    if (cell < 40) cell = 40;
    int ox = (g->w - cell * 4) / 2;
    int oy = 78;
    gfx::fillrect(s, ox - 6, oy - 6, cell * 4 + 12, cell * 4 + 12, 0x00BBADA0);
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            uint32_t v = g->board[y][x];
            int lvl = 0;
            uint32_t t = v;
            while (t > 1) { t >>= 1; lvl++; }
            uint32_t bg = (lvl >= 1 && lvl <= 15) ? TILE_COLORS[lvl - 1] : 0x00CDC1B4;
            gfx::fillrect(s, ox + x * cell, oy + y * cell, cell - 4, cell - 4, bg);
            if (v) {
                char num[16];
                ksprintf(num, sizeof(num), "%u", (unsigned)v);
                int fg = (v <= 4) ? 0x00776756 : 0x00F9F6F2;
                int scale = v >= 1024 ? 1 : 2;
                int tw = (int)strlen(num) * 8 * scale;
                gfx::text_scale(s, ox + x * cell + (cell - tw) / 2, oy + y * cell + cell / 2 - 8 * scale, num, fg, bg, scale);
            }
        }
    }
    if (g->over) {
        gfx::fillrect(s, 0, g->h / 2 - 20, g->w, 40, 0x00C00000);
        gfx::text(s, 10, g->h / 2 - 12, "GAME OVER - press R", color::WHITE, 0x00C00000);
    } else if (g->won) {
        gfx::fillrect(s, 0, g->h / 2 - 20, g->w, 40, 0x00008800);
        gfx::text(s, 10, g->h / 2 - 12, "YOU WIN 2048! press R", color::WHITE, 0x00008800);
    }
    gfx::text(s, 8, g->h - 16, "Arrows: move   R: restart", 0x00909090, 0x00FAF8EF);
}

void g_key(Window* w, const KeyEvent* e) {
    G2048* g = (G2048*)w->userdata;
    if (!e->down) return;
    if (e->ascii == 'r' || e->ascii == 'R') { g_spawn(*g); return; }
    if (g->over) return;
    bool changed = false;
    switch (e->keycode) {
    case KEY_LEFT: changed = g_move(*g, -1, 0); break;
    case KEY_RIGHT: changed = g_move(*g, 1, 0); break;
    case KEY_UP: changed = g_move(*g, 0, -1); break;
    case KEY_DOWN: changed = g_move(*g, 0, 1); break;
    default: return;
    }
    if (changed) {
        g_add_tile(*g);
        if (!g_can_move(*g)) g->over = true;
    }
}

void g_close(Window* w) {
    if (w->userdata) delete (G2048*)w->userdata;
    w->userdata = 0;
}

} // namespace

void game2048_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("2048", x, y, 340, 480);
    if (!w) return;
    G2048* g = new G2048();
    g->w = w->content_w;
    g->h = w->content_h;
    g_spawn(*g);
    w->userdata = g;
    w->on_paint = g_paint;
    w->on_key = g_key;
    w->on_close = g_close;
    g_wm->raise(w);
}

} // namespace nefu
