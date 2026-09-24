// nefuOS Tetris — classic falling-block game
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"

namespace nefu {

namespace {

const int TW = 10, TH = 20;
const uint32_t COLORS[7] = {
    0x0000C8FF, 0x00FFA500, 0x00FFD700, 0x0000AA00,
    0x00AA00FF, 0x00FF0000, 0x0000AAAA
};

// tetromino shapes: 4 rotations x 7 pieces x 4 cells (x,y)
const int8_t SHAPES[7][4][4][2] = {
    // I
    {{{0,1},{1,1},{2,1},{3,1}}, {{2,0},{2,1},{2,2},{2,3}}, {{0,2},{1,2},{2,2},{3,2}}, {{1,0},{1,1},{1,2},{1,3}}},
    // O
    {{{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}},
    // T
    {{{1,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{2,1},{1,2}}, {{0,1},{1,1},{2,1},{1,2}}, {{1,0},{0,1},{1,1},{1,2}}},
    // S
    {{{1,0},{2,0},{0,1},{1,1}}, {{1,0},{1,1},{2,1},{2,2}}, {{1,1},{2,1},{0,2},{1,2}}, {{0,0},{0,1},{1,1},{1,2}}},
    // Z
    {{{0,0},{1,0},{1,1},{2,1}}, {{2,0},{1,1},{2,1},{1,2}}, {{0,1},{1,1},{1,2},{2,2}}, {{1,0},{0,1},{1,1},{0,2}}},
    // L
    {{{0,0},{0,1},{0,2},{1,2}}, {{0,1},{1,1},{2,1},{0,0}}, {{0,0},{1,0},{1,1},{1,2}}, {{2,0},{0,1},{1,1},{2,1}}},
    // J
    {{{1,0},{1,1},{0,2},{1,2}}, {{0,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{1,2},{2,2}}, {{0,1},{1,1},{2,1},{2,2}}}
};

struct Tetris {
    int w, h;
    uint8_t board[TH][TW];
    int px, py, rot, piece;
    int next_piece;
    int score, level, lines;
    uint32_t last_drop;
    bool over, paused;
    uint8_t last_keys;
};

bool tet_collide(Tetris& g, int px, int py, int rot, int piece) {
    for (int c = 0; c < 4; c++) {
        int x = px + SHAPES[piece][rot][c][0];
        int y = py + SHAPES[piece][rot][c][1];
        if (x < 0 || x >= TW || y >= TH) return true;
        if (y >= 0 && g.board[y][x]) return true;
    }
    return false;
}

void tet_place(Tetris& g) {
    for (int c = 0; c < 4; c++) {
        int x = g.px + SHAPES[g.piece][g.rot][c][0];
        int y = g.py + SHAPES[g.piece][g.rot][c][1];
        if (y >= 0 && x >= 0 && x < TW && y < TH) g.board[y][x] = (uint8_t)(g.piece + 1);
    }
    // clear lines
    int cleared = 0;
    for (int y = TH - 1; y >= 0; y--) {
        bool full = true;
        for (int x = 0; x < TW; x++) if (!g.board[y][x]) { full = false; break; }
        if (full) {
            for (int yy = y; yy > 0; yy--) for (int x = 0; x < TW; x++) g.board[yy][x] = g.board[yy - 1][x];
            for (int x = 0; x < TW; x++) g.board[0][x] = 0;
            cleared++;
            y++; // re-check this row
        }
    }
    if (cleared) {
        static const int PTS[5] = {0, 100, 300, 500, 800};
        g.lines += cleared;
        g.score += PTS[cleared > 4 ? 4 : cleared] * (g.level + 1);
        g.level = g.lines / 10;
    }
    // spawn next
    g.piece = g.next_piece;
    g.next_piece = (int)((platform_tick_ms() / 7) % 7);
    g.px = 3; g.py = 0; g.rot = 0;
    if (tet_collide(g, g.px, g.py, g.rot, g.piece)) g.over = true;
}

void tet_spawn(Tetris& g) {
    g.piece = g.next_piece;
    g.next_piece = (int)((platform_tick_ms() / 11) % 7);
    g.px = 3; g.py = 0; g.rot = 0;
    if (tet_collide(g, g.px, g.py, g.rot, g.piece)) g.over = true;
}

void tet_reset(Tetris& g) {
    for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) g.board[y][x] = 0;
    g.score = 0; g.level = 0; g.lines = 0; g.over = false; g.paused = false;
    g.next_piece = (int)((platform_tick_ms() / 13) % 7);
    tet_spawn(g);
    g.last_drop = platform_tick_ms();
}

void tet_tick(Window* w) {
    Tetris* g = (Tetris*)w->userdata;
    if (g->over || g->paused) return;
    uint32_t now = platform_tick_ms();
    uint32_t speed = (uint32_t)(500 - g->level * 40);
    if (speed < 90) speed = 90;
    if (now - g->last_drop >= speed) {
        if (!tet_collide(*g, g->px, g->py + 1, g->rot, g->piece)) {
            g->py++;
        } else {
            tet_place(*g);
            if (g->over) return;
        }
        g->last_drop = now;
    }
}

void tet_rot(Tetris& g, int dir) {
    int nr = (g.rot + dir + 4) % 4;
    if (!tet_collide(g, g.px, g.py, nr, g.piece)) g.rot = nr;
    else {
        // wall kick: try shifts
        for (int dx = -1; dx <= 1; dx++) {
            if (!tet_collide(g, g.px + dx, g.py, nr, g.piece)) { g.px += dx; g.rot = nr; return; }
        }
    }
}

void tet_paint(Window* win) {
    Tetris* g = (Tetris*)win->userdata;
    Surface& s = win->back;
    s.fill(0x00101418);
    int cs = g->w / (TW + 6);
    if (cs < 12) cs = 12;
    int bx = (g->w - TW * cs) / 2;
    // board border
    gfx::rect(s, bx - 2, 2, TW * cs + 4, TH * cs + 4, 0x00506070);
    for (int y = 0; y < TH; y++) {
        for (int x = 0; x < TW; x++) {
            if (g->board[y][x]) {
                gfx::fillrect(s, bx + x * cs, 2 + y * cs, cs - 1, cs - 1, COLORS[g->board[y][x] - 1]);
            } else {
                gfx::fillrect(s, bx + x * cs, 2 + y * cs, cs - 1, cs - 1, 0x00182028);
            }
        }
    }
    // current piece
    for (int c = 0; c < 4; c++) {
        int x = g->px + SHAPES[g->piece][g->rot][c][0];
        int y = g->py + SHAPES[g->piece][g->rot][c][1];
        if (y >= 0) gfx::fillrect(s, bx + x * cs, 2 + y * cs, cs - 1, cs - 1, COLORS[g->piece]);
    }
    // side panel
    int sx = bx + TW * cs + 10;
    gfx::text(s, sx, 8, "Next:", color::TEXT, 0x00101418);
    for (int c = 0; c < 4; c++) {
        int x = 1 + SHAPES[g->next_piece][0][c][0];
        int y = 1 + SHAPES[g->next_piece][0][c][1];
        gfx::fillrect(s, sx + x * 14, 24 + y * 14, 12, 12, COLORS[g->next_piece]);
    }
    char buf[64];
    ksprintf(buf, sizeof(buf), "Score: %d", g->score);
    gfx::text(s, sx, 80, buf, color::TEXT, 0x00101418);
    ksprintf(buf, sizeof(buf), "Level: %d", g->level);
    gfx::text(s, sx, 100, buf, color::TEXT, 0x00101418);
    ksprintf(buf, sizeof(buf), "Lines: %d", g->lines);
    gfx::text(s, sx, 120, buf, color::TEXT, 0x00101418);
    gfx::text(s, sx, 150, "R = restart", 0x00888888, 0x00101418);
    gfx::text(s, sx, 168, "P = pause", 0x00888888, 0x00101418);
    gfx::text(s, sx, 186, "Arrows move", 0x00888888, 0x00101418);
    if (g->over) {
        gfx::fillrect(s, 0, g->h / 2 - 20, g->w, 40, 0x00C00000);
        gfx::text(s, 10, g->h / 2 - 12, "GAME OVER - press R", color::WHITE, 0x00C00000);
    } else if (g->paused) {
        gfx::fillrect(s, 0, g->h / 2 - 20, g->w, 40, 0x00404040);
        gfx::text(s, 10, g->h / 2 - 12, "PAUSED - press P", color::WHITE, 0x00404040);
    }
}

void tet_key(Window* w, const KeyEvent* e) {
    Tetris* g = (Tetris*)w->userdata;
    if (!e->down) return;
    if (e->ascii == 'r' || e->ascii == 'R') { tet_reset(*g); return; }
    if (e->ascii == 'p' || e->ascii == 'P') { g->paused = !g->paused; return; }
    if (g->over || g->paused) return;
    switch (e->keycode) {
    case KEY_LEFT: if (!tet_collide(*g, g->px - 1, g->py, g->rot, g->piece)) g->px--; break;
    case KEY_RIGHT: if (!tet_collide(*g, g->px + 1, g->py, g->rot, g->piece)) g->px++; break;
    case KEY_DOWN: if (!tet_collide(*g, g->px, g->py + 1, g->rot, g->piece)) { g->py++; g->score++; } break;
    case KEY_UP: tet_rot(*g, 1); break;
    default: break;
    }
}

void tet_close(Window* w) {
    if (w->userdata) delete (Tetris*)w->userdata;
    w->userdata = 0;
}

} // namespace

void tetris_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Tetris", x, y, 400, 460);
    if (!w) return;
    Tetris* g = new Tetris();
    g->w = w->content_w;
    g->h = w->content_h;
    tet_reset(*g);
    w->userdata = g;
    w->on_paint = tet_paint;
    w->on_tick = tet_tick;
    w->on_key = tet_key;
    w->on_close = tet_close;
    g_wm->raise(w);
}

} // namespace nefu
