// flappy.cpp —— nefuOS 像素鸟（Flappy Bird 克隆）
//
// 玩法：
//   空格 / ↑      扇翅（向上冲一下）
//   R             死亡后重开
//   N             恢复默认最高分
//
// 规则：
//   - 鸟受重力下落，每按一次空格给一个向上冲量
//   - 管道从右侧生成，上下两根之间有缺口
//   - 飞过一组管道得 1 分；撞管道/地面/天花板死亡
//   - 鸟的俯仰角随垂直速度变化（向上抬头、向下俯冲）
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "games2_util.h"

namespace nefu {

namespace {

using namespace nefu::games2;

const int FW = 300;
const int FH = 420;

const int BIRD_X = 70;
const int BIRD_R = 8;
const float GRAVITY = 0.35f;
const float FLAP_VY = -5.0f;
const float PIPE_SPEED = 1.8f;

const int PIPE_W = 46;
const int PIPE_GAP = 90;
const int PIPE_SPACING = 130;
const int PIPE_COUNT = 4;
const int GROUND_H = 30;

enum FlapState { FS_READY = 0, FS_PLAY, FS_DEAD };

struct Pipe {
    float x;
    int gap_y;
    bool scored;
};

struct Flappy {
    float bird_y;
    float bird_vy;
    float bird_angle;       // -1(抬头) .. +1(俯冲)
    Pipe pipes[PIPE_COUNT];
    int next_pipe_idx;
    int score;
    int best;
    int state;
    uint32_t last_tick;
    float ground_scroll;
    Rng rng;

    void reset();
    void flap();
    void step();
    void spawn_pipe(float x);
};

void Flappy::reset() {
    bird_y = FH / 2.0f;
    bird_vy = 0;
    bird_angle = 0;
    score = 0;
    state = FS_READY;
    next_pipe_idx = 0;
    ground_scroll = 0;
    rng.seed((uint32_t)(platform_tick_ms() & 0xFFFFu));
    for (int i = 0; i < PIPE_COUNT; i++) {
        pipes[i].x = -100;
        pipes[i].gap_y = 0;
        pipes[i].scored = true;
    }
    spawn_pipe((float)(FW + 40));
    spawn_pipe((float)(FW + 40 + PIPE_SPACING));
    spawn_pipe((float)(FW + 40 + PIPE_SPACING * 2));
}

void Flappy::spawn_pipe(float x) {
    Pipe& p = pipes[next_pipe_idx];
    next_pipe_idx = (next_pipe_idx + 1) % PIPE_COUNT;
    p.x = x;
    int min_y = 70;
    int max_y = FH - GROUND_H - PIPE_GAP - 40;
    p.gap_y = rng.range(min_y, max_y);
    p.scored = false;
}

void Flappy::flap() {
    if (state == FS_DEAD) return;
    if (state == FS_READY) state = FS_PLAY;
    bird_vy = FLAP_VY;
}

void Flappy::step() {
    if (state != FS_PLAY) return;

    bird_vy += GRAVITY;
    bird_y += bird_vy;

    // 俯仰角：根据 vy 映射到 -1..1
    float target = bird_vy / 6.0f;
    if (target < -1) target = -1;
    if (target > 1) target = 1;
    bird_angle = bird_angle + (target - bird_angle) * 0.2f;

    if (bird_y < BIRD_R) {
        bird_y = (float)BIRD_R;
        bird_vy = 0;
    }
    if (bird_y > FH - GROUND_H - BIRD_R) {
        bird_y = (float)(FH - GROUND_H - BIRD_R);
        state = FS_DEAD;
        if (score > best) best = score;
        return;
    }

    ground_scroll += PIPE_SPEED;
    if (ground_scroll >= 16) ground_scroll = 0;

    for (int i = 0; i < PIPE_COUNT; i++) pipes[i].x -= PIPE_SPEED;

    for (int i = 0; i < PIPE_COUNT; i++) {
        if (pipes[i].x < -PIPE_W - 10) {
            float max_x = -100;
            for (int j = 0; j < PIPE_COUNT; j++)
                if (pipes[j].x > max_x) max_x = pipes[j].x;
            spawn_pipe(max_x + PIPE_SPACING);
        }
    }

    for (int i = 0; i < PIPE_COUNT; i++) {
        Pipe& p = pipes[i];
        Rect top{ (int)p.x, 0, PIPE_W, p.gap_y };
        Rect bot{ (int)p.x, p.gap_y + PIPE_GAP, PIPE_W,
                  FH - GROUND_H - (p.gap_y + PIPE_GAP) };
        if (circle_rect_collide(BIRD_X, (int)bird_y, BIRD_R, top) ||
            circle_rect_collide(BIRD_X, (int)bird_y, BIRD_R, bot)) {
            state = FS_DEAD;
            if (score > best) best = score;
            return;
        }
        if (!p.scored && p.x + PIPE_W < BIRD_X - BIRD_R) {
            p.scored = true;
            score++;
        }
    }
}

static void flappy_paint(Window* w) {
    Flappy* g = (Flappy*)w->userdata;
    Surface& s = w->back;

    gfx::fillrect(s, 0, 0, FW, FH - GROUND_H, 0x0070C5CE);

    // 视差云层：3 朵白云，按时间缓慢左移
    uint32_t ct = platform_tick_ms();
    static const int CLOUD_Y[3] = { 40, 100, 65 };
    static const int CLOUD_SPD[3] = { 6, 12, 9 };   // 像素/秒
    static const int CLOUD_OFF[3] = { 0, 110, 200 };
    for (int i = 0; i < 3; i++) {
        float cx = FW - ((ct / 1000.0f) * CLOUD_SPD[i] + CLOUD_OFF[i]);
        cx = cx - (float)(int)((cx + 80) / (FW + 80)) * (FW + 80);
        int ix = (int)cx;
        gfx::fillcircle(s, ix, CLOUD_Y[i], 11, 0x00FFFFFF);
        gfx::fillcircle(s, ix + 13, CLOUD_Y[i] + 3, 8, 0x00FFFFFF);
        gfx::fillcircle(s, ix - 12, CLOUD_Y[i] + 4, 7, 0x00FFFFFF);
    }

    gfx::fillrect(s, 0, FH - GROUND_H, FW, GROUND_H, 0x00DED895);
    // 地面滚动纹理
    for (int x = (int)g->ground_scroll - 16; x < FW; x += 16) {
        gfx::fillrect(s, x, FH - GROUND_H, 8, 4, 0x00B8B060);
    }
    gfx::fillrect(s, 0, FH - GROUND_H, FW, 3, 0x00000000);

    for (int i = 0; i < PIPE_COUNT; i++) {
        Pipe& p = g->pipes[i];
        if (p.x < -PIPE_W || p.x > FW) continue;
        gfx::fillrect(s, (int)p.x, 0, PIPE_W, p.gap_y, 0x005EB94D);
        gfx::fillrect(s, (int)p.x, p.gap_y + PIPE_GAP, PIPE_W,
                      FH - GROUND_H - p.gap_y - PIPE_GAP, 0x005EB94D);
        gfx::fillrect(s, (int)p.x - 1, p.gap_y - 6, PIPE_W + 2, 6, 0x007DD860);
        gfx::fillrect(s, (int)p.x - 1, p.gap_y + PIPE_GAP, PIPE_W + 2, 6, 0x007DD860);
    }

    // 鸟：根据俯仰角画圆 + 眼睛偏移
    int bx = BIRD_X, by = (int)g->bird_y;
    gfx::fillcircle(s, bx, by, BIRD_R, 0x00FBD344);
    int eye_dx = (g->bird_angle > 0.3f) ? 3 : 4;
    int eye_dy = (g->bird_angle < -0.3f) ? -4 : -3;
    gfx::fillcircle(s, bx + eye_dx, by + eye_dy, 2, 0x00000000);

    char buf[32];
    ksprintf(buf, sizeof(buf), "%d", g->score);
    gfx::text_scale(s, FW / 2 - 8, 20, buf, 0x00FFFFFF, 0x00000000, 3);

    if (g->state == FS_READY) {
        draw_center_text(s, FW, FH / 2 - 30, "FLAPPY", 0x00FFFFFF, 0x00000000);
        draw_center_text(s, FW, FH / 2, "Press SPACE", 0x00FFFFFF, 0x00000000);
    } else if (g->state == FS_DEAD) {
        gfx::fillrect(s, 0, FH / 2 - 40, FW, 90, 0x00000000);
        draw_center_text(s, FW, FH / 2 - 30, "GAME OVER", 0x00FF4040, 0x00000000);
        ksprintf(buf, sizeof(buf), "Score: %d   Best: %d", g->score, g->best);
        draw_center_text(s, FW, FH / 2 - 5, buf, 0x00FFFFFF, 0x00000000);
        draw_center_text(s, FW, FH / 2 + 20, "Press R", 0x00FFFF40, 0x00000000);
    }
}

static void flappy_tick(Window* w) {
    Flappy* g = (Flappy*)w->userdata;
    uint32_t now = platform_tick_ms();
    if (now - g->last_tick < 16) return;
    g->last_tick = now;
    g->step();
}

static void flappy_key(Window* w, const KeyEvent* e) {
    Flappy* g = (Flappy*)w->userdata;
    if (!e->down) return;
    if (e->ascii == ' ' || e->keycode == KEY_UP) {
        g->flap();
        return;
    }
    if (e->ascii == 'r' || e->ascii == 'R') {
        if (g->state == FS_DEAD) g->reset();
        return;
    }
    if (e->ascii == 'n' || e->ascii == 'N') { g->best = 0; return; }
}

static void flappy_close(Window* w) {
    if (w->userdata) delete (Flappy*)w->userdata;
    w->userdata = 0;
}

} // namespace

void flappy_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Flappy", x, y, FW, FH);
    if (!w) return;
    Flappy* g = new Flappy();
    g->best = 0;
    g->reset();
    g->last_tick = platform_tick_ms();
    w->userdata = g;
    w->on_paint = flappy_paint;
    w->on_tick = flappy_tick;
    w->on_key = flappy_key;
    w->on_close = flappy_close;
    g_wm->raise(w);
}

} // namespace nefu
