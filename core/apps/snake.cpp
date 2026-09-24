// nefuOS Snake Game
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../klib/klib.h"
#include <cstring>

namespace nefu {

namespace {

const int CELL = 16;
const int GRID_W = 25;
const int GRID_H = 25;

struct SnakeState {
    int x[100];
    int y[100];
    int len;
    int dx, dy;
    int food_x, food_y;
    int score;
    bool game_over;
    uint32_t last_tick;

    SnakeState() : len(3), dx(1), dy(0), score(0), game_over(false) {
        x[0] = 10; y[0] = 10;
        x[1] = 9; y[1] = 10;
        x[2] = 8; y[2] = 10;
        food_x = 15; food_y = 15;
        last_tick = 0;
    }
};

static void snake_paint(Window* w) {
    SnakeState* st = (SnakeState*)w->userdata;
    Surface& s = w->back;

    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, 0x000000);

    // Draw snake
    for (int i = 0; i < st->len; i++) {
        int color = (i == 0) ? 0x00FF00 : 0x00CC00;
        gfx::fillrect(s, st->x[i] * CELL, st->y[i] * CELL, CELL - 1, CELL - 1, color);
    }

    // Draw food
    gfx::fillrect(s, st->food_x * CELL, st->food_y * CELL, CELL - 1, CELL - 1, 0xFF0000);

    // Score
    char score[32];
    ksprintf(score, sizeof(score), "Score: %d", st->score);
    gfx::text(s, 5, w->content_h - 15, score, 0xFFFFFF, 0x000000);

    if (st->game_over) {
        gfx::text(s, 100, 120, "GAME OVER!", 0xFF0000, 0x000000);
    }
}

static void snake_tick(SnakeState* st) {
    if (st->game_over) return;

    // Move body
    for (int i = st->len - 1; i > 0; i--) {
        st->x[i] = st->x[i - 1];
        st->y[i] = st->y[i - 1];
    }

    // Move head
    st->x[0] += st->dx;
    st->y[0] += st->dy;

    // Check wall collision
    if (st->x[0] < 0 || st->x[0] >= GRID_W || st->y[0] < 0 || st->y[0] >= GRID_H) {
        st->game_over = true;
    }

    // Check self collision
    for (int i = 1; i < st->len; i++) {
        if (st->x[0] == st->x[i] && st->y[0] == st->y[i]) {
            st->game_over = true;
        }
    }

    // Check food
    if (st->x[0] == st->food_x && st->y[0] == st->food_y) {
        st->score += 10;
        if (st->len < 100) st->len++;
        st->food_x = (st->x[0] + 7) % GRID_W;
        st->food_y = (st->y[0] + 13) % GRID_H;
    }
}

// Periodic heartbeat: advance the game at a fixed rate (driven by WM::tick)
static void snake_tick_driver(Window* w) {
    SnakeState* st = (SnakeState*)w->userdata;
    if (!st) return;
    uint32_t now = platform_tick_ms();
    if (now - st->last_tick < 110) return;   // ~9 steps/sec
    st->last_tick = now;
    snake_tick(st);
}

static void snake_key(Window* w, const KeyEvent* e) {
    SnakeState* st = (SnakeState*)w->userdata;
    if (!e->down) return;

    switch (e->keycode) {
        case KEY_UP:    st->dx = 0; st->dy = -1; break;
        case KEY_DOWN:  st->dx = 0; st->dy = 1; break;
        case KEY_LEFT:  st->dx = -1; st->dy = 0; break;
        case KEY_RIGHT: st->dx = 1; st->dy = 0; break;
    }
    // R key = restart
    if (e->ascii == 'r' || e->ascii == 'R') {
        st->len = 3; st->dx = 1; st->dy = 0;
        st->x[0] = 10; st->y[0] = 10;
        st->x[1] = 9; st->y[1] = 10;
        st->x[2] = 8; st->y[2] = 10;
        st->score = 0; st->game_over = false;
    }
}

static void snake_close(Window* w) {
    SnakeState* st = (SnakeState*)w->userdata;
    delete st;
}

} // namespace

void snake_launch() {
    SnakeState* st = new SnakeState();
    st->last_tick = platform_tick_ms();
    Window* w = g_wm->create_window("Snake", 100, 100, GRID_W * CELL, GRID_H * CELL + 20);
    w->userdata = st;
    w->on_paint = snake_paint;
    w->on_tick = snake_tick_driver;
    w->on_key = snake_key;
    w->on_close = snake_close;
    g_wm->raise(w);
}

} // namespace nefu