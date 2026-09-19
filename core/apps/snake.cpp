// nefuOS snake - WM window (surface back + direct blit), arrows, timer in on_paint
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"

namespace nefu {

struct SnakeCore {
    int cols, rows;
    int body[400][2];
    int len;
    int dir;
    int next_dir;
    int food[2];
    int score;
    uint32_t last_move;
    bool over;
    bool paused;
};

static void snake_reset_food(SnakeCore* st) {
    for (int attempt = 0; attempt < 200; attempt++) {
        int fx = (int)((platform_tick_ms() * 2654435761u + attempt * 97) >> 13) % st->cols;
        int fy = (int)((platform_tick_ms() * 40503u + attempt * 131) >> 13) % st->rows;
        if (fx < 0) fx = 0;
        if (fy < 0) fy = 0;
        bool on = false;
        for (int i = 0; i < st->len; i++) {
            if (st->body[i][0] == fx && st->body[i][1] == fy) { on = true; break; }
        }
        if (!on) { st->food[0] = fx; st->food[1] = fy; return; }
    }
    st->food[0] = -1; st->food[1] = -1;
}

static void snake_tick(SnakeCore* st) {
    if (st->over || st->paused) return;
    uint32_t now = platform_tick_ms();
    if (now - st->last_move < 160) return;
    st->last_move = now;
    st->dir = st->next_dir;
    int nx = st->body[0][0], ny = st->body[0][1];
    switch (st->dir) {
    case 0: ny--; break;
    case 1: ny++; break;
    case 2: nx--; break;
    case 3: nx++; break;
    }
    if (nx < 0 || ny < 0 || nx >= st->cols || ny >= st->rows) { st->over = true; return; }
    for (int i = 0; i < st->len; i++) {
        if (st->body[i][0] == nx && st->body[i][1] == ny) { st->over = true; return; }
    }
    for (int i = st->len - 1; i > 0; i--) { st->body[i][0] = st->body[i - 1][0]; st->body[i][1] = st->body[i - 1][1]; }
    st->body[0][0] = nx; st->body[0][1] = ny;
    if (st->food[0] == nx && st->food[1] == ny) {
        if (st->len < 400) {
            st->body[st->len][0] = st->body[st->len - 1][0];
            st->body[st->len][1] = st->body[st->len - 1][1];
            st->len++;
        }
        st->score += 10;
        snake_reset_food(st);
    }
}

struct SnakeLvState {
    SnakeCore core;
    int w, h;
};

static void snake_draw(Surface& s, SnakeCore* c) {
    int w = s.width, h = s.height;
    int cw = c->cols, ch = c->rows;
    int cs = 12;
    if (cw * cs + 16 > w || ch * cs + 40 > h) { cs = 8; }
    s.fill(0x00101418);
    int ox = (w - cw * cs) / 2, oy = 24 + (h - 40 - ch * cs) / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;
    gfx::rect(s, ox, oy, cw * cs, ch * cs, 0x0030465A);
    for (int x = 1; x < cw; x++) gfx::vline(s, ox + x * cs, oy, oy + ch * cs - 1, 0x00182638);
    for (int y = 1; y < ch; y++) gfx::hline(s, ox, ox + cw * cs - 1, oy + y * cs, 0x00182638);
    if (c->food[0] >= 0) {
        gfx::fillrect(s, ox + c->food[0] * cs + 2, oy + c->food[1] * cs + 2, cs - 4, cs - 4, color::RED);
    }
    for (int i = c->len - 1; i >= 0; i--) {
        int px = ox + c->body[i][0] * cs, py = oy + c->body[i][1] * cs;
        uint32_t col = (i == 0) ? 0x0073E06B : 0x0047B33D;
        gfx::fillrect(s, px + 1, py + 1, cs - 2, cs - 2, col);
    }
    char buf[48];
    ksprintf(buf, sizeof(buf), "Score: %d   Len: %d", c->score, c->len);
    gfx::text(s, 8, 4, buf, color::WHITE, 0x00101418);
    if (c->over) gfx::text(s, 10, h - 20, "GAME OVER - press R to restart", color::RED, 0x00101418);
    else if (c->paused) gfx::text(s, 10, h - 20, "PAUSED - press P", color::YELLOW, 0x00101418);
}

static void snake_wm_paint(Window* w) {
    SnakeLvState* st = (SnakeLvState*)w->userdata;
    if (!st) return;
    if (platform_tick_ms() - st->core.last_move >= 160) snake_tick(&st->core);
    snake_draw(w->back, &st->core);
}

static void snake_wm_key(Window* w, const KeyEvent* e) {
    SnakeLvState* st = (SnakeLvState*)w->userdata;
    if (!st) return;
    SnakeCore* c = &st->core;
    if (!e->down) return;
    switch (e->keycode) {
    case KEY_UP:    if (c->dir != 1) c->next_dir = 0; break;
    case KEY_DOWN:  if (c->dir != 0) c->next_dir = 1; break;
    case KEY_LEFT:  if (c->dir != 3) c->next_dir = 2; break;
    case KEY_RIGHT: if (c->dir != 2) c->next_dir = 3; break;
    default: break;
    }
    char a = e->ascii;
    if (a == 'p' || a == 'P') { if (!c->over) c->paused = !c->paused; }
    if (a == 'r' || a == 'R') {
        c->len = 4;
        c->body[0][0] = c->cols / 2; c->body[0][1] = c->rows / 2;
        c->body[1][0] = c->body[0][0] - 1; c->body[1][1] = c->body[0][1];
        c->body[2][0] = c->body[0][0] - 2; c->body[2][1] = c->body[0][1];
        c->body[3][0] = c->body[0][0] - 3; c->body[3][1] = c->body[0][1];
        c->dir = 3; c->next_dir = 3;
        c->score = 0;
        c->over = false;
        c->paused = false;
        c->last_move = platform_tick_ms();
        snake_reset_food(c);
    }
}

void snake_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Snake", x, y, 430, 470);
    if (!w) return;
    SnakeLvState* st = new SnakeLvState();
    st->w = w->content_w;
    st->h = w->content_h;
    w->userdata = st;
    w->on_paint = snake_wm_paint;
    w->on_key = snake_wm_key;

    SnakeCore* c = &st->core;
    c->cols = 30; c->rows = 26;
    c->len = 4;
    c->body[0][0] = 15; c->body[0][1] = 13;
    c->body[1][0] = 14; c->body[1][1] = 13;
    c->body[2][0] = 13; c->body[2][1] = 13;
    c->body[3][0] = 12; c->body[3][1] = 13;
    c->dir = 3; c->next_dir = 3;
    c->score = 0;
    c->last_move = platform_tick_ms();
    c->over = false;
    c->paused = false;
    snake_reset_food(c);
    g_wm->raise(w);
}
} // namespace nefu
