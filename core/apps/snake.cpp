// nefuOS snake：，，hit wall/hit self ends game
#include "apps.h"
#include "../gui/gfx.h"
#include "../platform.h"

namespace nefu {

struct SnakeState {
    int cols, rows;
    int body[400][2];      // snake body coords
    int len;
    int dir;               // 0up 1down 2left 3right
    int next_dir;
    int food[2];
    int score;
    uint32_t last_move;
    bool over;
    bool paused;
    Window* win;
};

static void snake_reset_food(SnakeState* st) {

    for (int attempt = 0; attempt < 200; attempt++) {
        int fx = (int)((platform_tick_ms() * 2654435761u + attempt * 97) >> 13) % st->cols;
        int fy = (int)((platform_tick_ms() * 40503u + attempt * 131) >> 13) % st->rows;
        if (fx < 0) { fx = 0; }
        if (fy < 0) { fy = 0; }
        bool on = false;
        for (int i = 0; i < st->len; i++) {
            if (st->body[i][0] == fx && st->body[i][1] == fy) { on = true; break; }
        }
        if (!on) { st->food[0] = fx; st->food[1] = fy; return; }
    }
    st->food[0] = -1; st->food[1] = -1; // full
}

static void snake_paint(Window* w) {
    SnakeState* st = (SnakeState*)w->userdata;
    Surface& s = w->back;
    s.fill(0x00101418);
    int cw = st->cols, ch = st->rows;
    int cs = 12; // cell size
    // centered
    int ox = (s.width - cw * cs) / 2;
    int oy = (s.height - ch * cs) / 2;
    if (ox < 0) { ox = 0; }
    if (oy < 0) { oy = 0; }
    // grid
    gfx::rect(s, ox, oy, cw * cs, ch * cs, 0x0030465A);
    for (int x = 1; x < cw; x++) gfx::vline(s, ox + x * cs, oy, oy + ch * cs - 1, 0x00182638);
    for (int y = 1; y < ch; y++) gfx::hline(s, ox, ox + cw * cs - 1, oy + y * cs, 0x00182638);

    if (st->food[0] >= 0) {
        gfx::fillrect(s, ox + st->food[0] * cs + 2, oy + st->food[1] * cs + 2, cs - 4, cs - 4, color::RED);
    }
    // snake body（head bright green）
    for (int i = st->len - 1; i >= 0; i--) {
        int px = ox + st->body[i][0] * cs, py = oy + st->body[i][1] * cs;
        uint32_t c = (i == 0) ? 0x0073E06B : 0x0047B33D;
        gfx::fillrect(s, px + 1, py + 1, cs - 2, cs - 2, c);
    }
    char buf[48];
    ksprintf(buf, sizeof(buf), "Score: %d   Len: %d", st->score, st->len);
    gfx::text(s, 8, 6, buf, color::WHITE, 0x00101418);
    if (st->over) {
        gfx::text(s, 10, s.height - 22, "GAME OVER - press R to restart", color::RED, 0x00101418);
    } else if (st->paused) {
        gfx::text(s, 10, s.height - 22, "PAUSED - press P", color::YELLOW, 0x00101418);
    }
}

static void snake_tick(SnakeState* st) {
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
    // hit wall
    if (nx < 0 || ny < 0 || nx >= st->cols || ny >= st->rows) { st->over = true; return; }
    // hit self
    for (int i = 0; i < st->len; i++) {
        if (st->body[i][0] == nx && st->body[i][1] == ny) { st->over = true; return; }
    }
    // move
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

static void snake_key(Window* w, const KeyEvent* e) {
    SnakeState* st = (SnakeState*)w->userdata;
    if (!e->down) return;
    switch (e->keycode) {
    case KEY_UP: if (st->dir != 1) st->next_dir = 0; break;
    case KEY_DOWN: if (st->dir != 0) st->next_dir = 1; break;
    case KEY_LEFT: if (st->dir != 3) st->next_dir = 2; break;
    case KEY_RIGHT: if (st->dir != 2) st->next_dir = 3; break;
    default: break;
    }
    if (e->ascii == 'p' || e->ascii == 'P') {
        if (!st->over) st->paused = !st->paused;
    }
    if (e->ascii == 'r' || e->ascii == 'R') {
        // restart
        st->len = 4;
        st->body[0][0] = st->cols / 2; st->body[0][1] = st->rows / 2;
        st->body[1][0] = st->body[0][0] - 1; st->body[1][1] = st->body[0][1];
        st->body[2][0] = st->body[0][0] - 2; st->body[2][1] = st->body[0][1];
        st->body[3][0] = st->body[0][0] - 3; st->body[3][1] = st->body[0][1];
        st->dir = 3; st->next_dir = 3;
        st->score = 0;
        st->over = false;
        st->paused = false;
        st->last_move = platform_tick_ms();
        snake_reset_food(st);
    }
}

static void snake_close(Window* w) {
    if (w->userdata) delete (SnakeState*)w->userdata;
    w->userdata = 0;
}

static void snake_paint_auto(Window* w) {
    snake_tick((SnakeState*)w->userdata);
    snake_paint(w);
}

void snake_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Snake", x, y, 420, 420);
    if (!w) return;
    SnakeState* st = new SnakeState();
    st->win = w;
    st->cols = 30; st->rows = 26;
    st->len = 4;
    st->body[0][0] = 15; st->body[0][1] = 13;
    st->body[1][0] = 14; st->body[1][1] = 13;
    st->body[2][0] = 13; st->body[2][1] = 13;
    st->body[3][0] = 12; st->body[3][1] = 13;
    st->dir = 3; st->next_dir = 3;
    st->score = 0;
    st->last_move = platform_tick_ms();
    st->over = false;
    st->paused = false;
    snake_reset_food(st);
    w->userdata = st;
    w->on_paint = snake_paint_auto; // advance then redraw each frame
    w->on_key = snake_key;
    w->on_close = snake_close;
}

} // namespace nefu
