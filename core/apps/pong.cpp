// pong.cpp —— nefuOS 乒乓球（Pong）
//
// 玩法：
//   左挡板：W / S        上/下
//   右挡板：↑ / ↓        上/下（双人模式）
//   T                   切换右侧：AI <-> 双人
//   D                   切换 AI 难度：易 / 中 / 难
//   空格                发球
//   R                   重开
//
// 规则：
//   - 球越过对方底线得 1 分，先得 7 分者获胜
//   - 每次击中挡板球速略微提升，反弹角随击中位置变化
//   - AI 难度：易=慢且误差大、中=正常、难=极快
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "games2_util.h"

namespace nefu {

namespace {

using namespace nefu::games2;

const int PW = 640;
const int PH = 400;

const int PADDLE_W = 10;
const int PADDLE_H = 70;
const int PADDLE_MARGIN = 20;
const int PADDLE_SPEED = 5;
const int BALL_R = 7;

const int WIN_SCORE = 7;

enum AIDiff { AI_EASY = 0, AI_NORMAL = 1, AI_HARD = 2 };
const char* DIFF_NAMES[3] = { "EASY", "NORMAL", "HARD" };

enum PongState { PG_READY = 0, PG_PLAY, PG_POINT, PG_GAMEOVER };

struct Pong {
    int p1_y, p2_y;
    bool p1_up, p1_down;
    bool p2_up, p2_down;

    float ball_x, ball_y;
    float ball_vx, ball_vy;

    int score1, score2;
    int state;
    bool p2_is_ai;
    AIDiff diff;

    uint32_t last_tick;
    uint32_t point_timer;
    int serve_dir;
    Trail trail;
    Rng rng;

    void reset();
    void serve();
    void step();
    void ai_step();
    void move_paddles();
};

void Pong::reset() {
    p1_y = (PH - PADDLE_H) / 2;
    p2_y = (PH - PADDLE_H) / 2;
    p1_up = p1_down = false;
    p2_up = p2_down = false;
    score1 = score2 = 0;
    state = PG_READY;
    p2_is_ai = true;
    diff = AI_NORMAL;
    serve_dir = 1;
    ball_x = PW / 2;
    ball_y = PH / 2;
    ball_vx = ball_vy = 0;
    trail.reset();
    rng.seed((uint32_t)(platform_tick_ms() & 0xFFFFu));
}

void Pong::serve() {
    ball_x = PW / 2.0f;
    ball_y = PH / 2.0f;
    float speed = 4.0f;
    ball_vx = (float)serve_dir * speed;
    ball_vy = (float)rng.range(-20, 20) * 0.1f;
    state = PG_PLAY;
}

void Pong::ai_step() {
    if (!p2_is_ai) return;
    int target = (int)ball_y - PADDLE_H / 2;
    int center = p2_y + PADDLE_H / 2;

    int speed;
    int error;
    if (diff == AI_EASY)  { speed = 2; error = 20; }
    else if (diff == AI_HARD) { speed = 6; error = 2; }
    else                  { speed = 3; error = 8; }

    // 球向左飞时 AI 回中，向右飞时追球
    int aim = center;
    if (ball_vx > 0) aim = target + rng.range(-error, error);

    int d = aim - center;
    if (d < -speed) p2_y -= speed;
    else if (d > speed) p2_y += speed;

    if (p2_y < 30) p2_y = 30;
    if (p2_y > PH - PADDLE_H - 30) p2_y = PH - PADDLE_H - 30;
}

void Pong::move_paddles() {
    if (p1_up)   p1_y -= PADDLE_SPEED;
    if (p1_down) p1_y += PADDLE_SPEED;
    if (p1_y < 30) p1_y = 30;
    if (p1_y > PH - PADDLE_H - 30) p1_y = PH - PADDLE_H - 30;

    if (!p2_is_ai) {
        if (p2_up)   p2_y -= PADDLE_SPEED;
        if (p2_down) p2_y += PADDLE_SPEED;
        if (p2_y < 30) p2_y = 30;
        if (p2_y > PH - PADDLE_H - 30) p2_y = PH - PADDLE_H - 30;
    }
}

void Pong::step() {
    if (state != PG_PLAY) return;

    move_paddles();
    ai_step();

    trail.push(ball_x, ball_y);
    ball_x += ball_vx;
    ball_y += ball_vy;

    if (ball_y < BALL_R + 30) {
        ball_y = (float)(BALL_R + 30);
        ball_vy = -ball_vy;
    } else if (ball_y > PH - BALL_R - 10) {
        ball_y = (float)(PH - BALL_R - 10);
        ball_vy = -ball_vy;
    }

    Rect left{ PADDLE_MARGIN, p1_y, PADDLE_W, PADDLE_H };
    if (ball_vx < 0 && circle_rect_collide((int)ball_x, (int)ball_y, BALL_R, left)) {
        ball_x = (float)(PADDLE_MARGIN + PADDLE_W + BALL_R);
        float hit = (ball_y - (p1_y + PADDLE_H / 2.0f)) / (PADDLE_H / 2.0f);
        float speed = 4.0f + 0.15f * (float)(score1 + score2);
        ball_vx = speed;
        ball_vy = hit * speed * 0.8f;
    }

    Rect right{ PW - PADDLE_MARGIN - PADDLE_W, p2_y, PADDLE_W, PADDLE_H };
    if (ball_vx > 0 && circle_rect_collide((int)ball_x, (int)ball_y, BALL_R, right)) {
        ball_x = (float)(PW - PADDLE_MARGIN - PADDLE_W - BALL_R);
        float hit = (ball_y - (p2_y + PADDLE_H / 2.0f)) / (PADDLE_H / 2.0f);
        float speed = 4.0f + 0.15f * (float)(score1 + score2);
        ball_vx = -speed;
        ball_vy = hit * speed * 0.8f;
    }

    if (ball_x < -BALL_R) {
        score2++;
        serve_dir = -1;
        state = PG_POINT;
        point_timer = platform_tick_ms();
    } else if (ball_x > PW + BALL_R) {
        score1++;
        serve_dir = 1;
        state = PG_POINT;
        point_timer = platform_tick_ms();
    }

    if (score1 >= WIN_SCORE || score2 >= WIN_SCORE) state = PG_GAMEOVER;
}

static void pong_paint(Window* w) {
    Pong* g = (Pong*)w->userdata;
    Surface& s = w->back;

    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, 0x00000000);

    for (int y = 30; y < PH - 10; y += 20) {
        gfx::fillrect(s, PW / 2 - 1, y, 2, 10, 0x00FFFFFF);
    }

    gfx::fillrect(s, PADDLE_MARGIN, g->p1_y, PADDLE_W, PADDLE_H, 0x00FFFFFF);
    gfx::fillrect(s, PW - PADDLE_MARGIN - PADDLE_W, g->p2_y, PADDLE_W, PADDLE_H,
                  g->p2_is_ai ? 0x00FF6060 : 0x0060C0FF);

    g->trail.draw(s, 0x00AA9930);
    gfx::fillcircle(s, (int)g->ball_x, (int)g->ball_y, BALL_R, 0x00FFFF40);

    char buf[64];
    ksprintf(buf, sizeof(buf), "%d", g->score1);
    gfx::text(s, PW / 2 - 60, 6, buf, 0x00FFFFFF, 0x00000000);
    ksprintf(buf, sizeof(buf), "%d", g->score2);
    gfx::text(s, PW / 2 + 40, 6, buf, 0x00FFFFFF, 0x00000000);

    if (g->state == PG_READY) {
        draw_center_text(s, w->content_w, PH / 2 - 40, "PONG", 0x00FFFFFF, 0x00000000);
        draw_center_text(s, w->content_w, PH / 2 - 20,
                         g->p2_is_ai ? "Right: AI" : "Right: Player 2",
                         0x00AAAAAA, 0x00000000);
        draw_center_text(s, w->content_w, PH / 2,
                         DIFF_NAMES[g->diff], 0x00FFFF40, 0x00000000);
        draw_center_text(s, w->content_w, PH / 2 + 20,
                         "Press SPACE to start", 0x00FFFFFF, 0x00000000);
    } else if (g->state == PG_GAMEOVER) {
        const char* win = (g->score1 >= WIN_SCORE) ? "LEFT WINS!" : "RIGHT WINS!";
        draw_center_text(s, w->content_w, PH / 2 - 10, win, 0x00FFFF40, 0x00000000);
        draw_center_text(s, w->content_w, PH / 2 + 10,
                         "Press R to restart", 0x00FFFFFF, 0x00000000);
    }
}

static void pong_tick(Window* w) {
    Pong* g = (Pong*)w->userdata;
    uint32_t now = platform_tick_ms();
    if (now - g->last_tick < 8) return;
    g->last_tick = now;
    if (g->state == PG_POINT && now - g->point_timer > 1000) g->serve();
    g->step();
}

static void pong_key(Window* w, const KeyEvent* e) {
    Pong* g = (Pong*)w->userdata;
    bool down = e->down;

    if (e->ascii == 'w' || e->ascii == 'W') { g->p1_up = down; return; }
    if (e->ascii == 's' || e->ascii == 'S') { g->p1_down = down; return; }
    if (e->keycode == KEY_UP)   { g->p2_up = down; return; }
    if (e->keycode == KEY_DOWN) { g->p2_down = down; return; }

    if (!down) return;
    if (e->ascii == ' ' || e->keycode == KEY_ENTER) {
        if (g->state == PG_READY || g->state == PG_GAMEOVER) {
            g->reset();
            g->serve();
        }
        return;
    }
    if (e->ascii == 'r' || e->ascii == 'R') { g->reset(); return; }
    if (e->ascii == 't' || e->ascii == 'T') {
        g->p2_is_ai = !g->p2_is_ai;
        g->p2_up = g->p2_down = false;
        return;
    }
    if (e->ascii == 'd' || e->ascii == 'D') {
        g->diff = (AIDiff)((g->diff + 1) % 3);
        return;
    }
}

static void pong_close(Window* w) {
    if (w->userdata) delete (Pong*)w->userdata;
    w->userdata = 0;
}

} // namespace

void pong_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Pong", x, y, PW, PH);
    if (!w) return;
    Pong* g = new Pong();
    g->reset();
    g->last_tick = platform_tick_ms();
    w->userdata = g;
    w->on_paint = pong_paint;
    w->on_tick = pong_tick;
    w->on_key = pong_key;
    w->on_close = pong_close;
    g_wm->raise(w);
}

} // namespace nefu
