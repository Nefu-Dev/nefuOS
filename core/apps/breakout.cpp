// breakout.cpp —— nefuOS 打砖块（Breakout）
//
// 玩法：
//   ←/→ 或 A/D   移动挡板
//   空格         发球 / 激光射击 / 下一关
//   R            重开当前局
//   P            暂停
//
// 规则：
//   - 砖块分四种：普通(1血)、银色(2血)、金色(3血)、钢(不可打碎)
//   - 打碎砖块有概率掉落道具：
//       W = 挡板变宽 10 秒
//       M = 三分球（额外两个球）
//       L = 激光（按空格向上发射）
//   - 球落到底线以下损失一条命，共 3 命
//   - 清空所有可碎砖块进入下一关，球速提升
//
// 实现要点：
//   - 挡板/球/砖块全部用 AABB + 圆-矩形碰撞
//   - 球撞砖时按 x/y 重叠深度判断从哪一面击中，决定反弹轴向
//   - -fno-builtin 下不依赖 memset，棋盘清零用手动循环
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "games2_util.h"

namespace nefu {

namespace {

using namespace nefu::games2;

// 窗口尺寸
const int BW = 480;
const int BH = 560;

// 砖块阵列
const int BRICK_COLS = 10;
const int BRICK_ROWS = 6;
const int BRICK_W = 44;
const int BRICK_H = 16;
const int BRICK_TOP = 50;
const int BRICK_LEFT = 20;

// 挡板
const int PADDLE_W = 64;
const int PADDLE_H = 10;
const int PADDLE_Y = BH - 30;
const int PADDLE_SPEED = 6;
const int PADDLE_WIDE_W = 96;

// 球
const int BALL_R = 6;
const int MAX_BALLS = 3;

// 道具
enum PowerType { PW_NONE = 0, PW_WIDE, PW_MULTI, PW_LASER };
const int POWER_FALL_SPEED = 2;
const int POWER_W = 16;
const int POWER_H = 12;

// 砖块类型
enum BrickType { BR_NORMAL = 1, BR_SILVER = 2, BR_GOLD = 3, BR_STEEL = 9 };

enum BREAK_STATE {
    BS_READY = 0,
    BS_PLAY,
    BS_PAUSE,
    BS_LEVEL_CLEAR,
    BS_GAME_OVER,
    BS_WIN
};

struct Ball {
    float x, y;
    float vx, vy;
    bool active;
};

struct PowerUp {
    float x, y;
    int type;
    bool active;
};

struct Breakout {
    int paddle_x;
    int paddle_w;
    uint32_t wide_until;

    Ball balls[MAX_BALLS];
    int brick_hp[BRICK_ROWS][BRICK_COLS];   // 0=空，>0=剩余血量
    int alive_bricks;

    PowerUp powers[4];
    bool laser_ready;
    uint32_t laser_until;

    ParticleSystem particles;

    int score;
    int lives;
    int level;
    int state;

    bool key_left, key_right;
    uint32_t last_tick;
    Rng rng;

    void reset_bricks();
    void reset_ball_on_paddle();
    void start_game();
    void launch_ball();
    void step();
    void step_ball(Ball& b);
    void move_paddle(int dx);
    void spawn_powerup(int x, int y);
    void apply_powerup(int type);
    bool ball_hits_brick(Ball& b, int row, int col);
    void on_brick_hit(Ball& b, int row, int col);
};

void Breakout::reset_bricks() {
    alive_bricks = 0;
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            int hp = 0;   // 0 = 空位
            switch (level) {
            case 1:
                // 关卡 1：金/银/普通三层，钢砖隔列穿插
                if (r == 4 && c % 3 == 1) hp = BR_STEEL;
                else if (r < 2) hp = BR_GOLD;
                else if (r < 4) hp = BR_SILVER;
                else hp = BR_NORMAL;
                break;
            case 2:
                // 关卡 2：沙漏形
                if (r == 0 || r == 5) hp = (c >= 2 && c <= 7) ? BR_SILVER : 0;
                else if (r == 1 || r == 4) hp = (c >= 1 && c <= 8) ? BR_NORMAL : 0;
                else hp = BR_GOLD;
                break;
            case 3:
                // 关卡 3：棋盘格 + 钢柱
                if ((r + c) % 2 == 0) hp = BR_NORMAL;
                else hp = (c % 4 == 0) ? BR_STEEL : BR_SILVER;
                break;
            case 4:
                // 关卡 4：两侧钢墙，中间金色斜带
                if (c == 0 || c == 9) hp = BR_STEEL;
                else if (r == c - 1 || r == 4 - (c - 1)) hp = BR_GOLD;
                else hp = BR_NORMAL;
                break;
            default:
                // 关卡 5+：全金 + 钢网格
                if (r % 2 == 1 && c % 2 == 1) hp = BR_STEEL;
                else hp = BR_GOLD;
                break;
            }
            brick_hp[r][c] = hp;
            if (hp != 0 && hp != BR_STEEL) alive_bricks++;
        }
    }
}

void Breakout::reset_ball_on_paddle() {
    for (int i = 0; i < MAX_BALLS; i++) balls[i].active = false;
    balls[0].x = (float)(paddle_x + paddle_w / 2);
    balls[0].y = (float)(PADDLE_Y - BALL_R - 1);
    balls[0].vx = 0;
    balls[0].vy = 0;
    balls[0].active = true;
    state = BS_READY;
}

void Breakout::start_game() {
    score = 0;
    lives = 3;
    level = 1;
    paddle_x = (BW - PADDLE_W) / 2;
    paddle_w = PADDLE_W;
    wide_until = 0;
    laser_until = 0;
    laser_ready = false;
    for (int i = 0; i < 4; i++) powers[i].active = false;
    rng.seed((uint32_t)(platform_tick_ms() & 0xFFFFu));
    particles.reset();
    reset_bricks();
    reset_ball_on_paddle();
    key_left = key_right = false;
}

void Breakout::launch_ball() {
    if (state != BS_READY) return;
    Ball& b = balls[0];
    float speed = 3.0f + (float)level * 0.4f;
    int dir = rng.chance() ? 1 : -1;
    b.vx = speed * 0.3f * (float)dir;
    b.vy = -speed;
    state = BS_PLAY;
}

void Breakout::move_paddle(int dx) {
    paddle_x += dx;
    if (paddle_x < 0) paddle_x = 0;
    if (paddle_x > BW - paddle_w) paddle_x = BW - paddle_w;
    if (state == BS_READY) {
        balls[0].x = (float)(paddle_x + paddle_w / 2);
    }
}

void Breakout::spawn_powerup(int x, int y) {
    for (int i = 0; i < 4; i++) {
        if (powers[i].active) continue;
        int roll = rng.range(0, 99);
        int t = (roll < 30) ? PW_WIDE : (roll < 60) ? PW_MULTI : PW_LASER;
        powers[i].x = (float)x;
        powers[i].y = (float)y;
        powers[i].type = t;
        powers[i].active = true;
        return;
    }
}

void Breakout::apply_powerup(int type) {
    if (type == PW_WIDE) {
        paddle_w = PADDLE_WIDE_W;
        wide_until = platform_tick_ms() + 10000;
    } else if (type == PW_MULTI) {
        // 给第一个活跃球复制两个 45 度角
        for (int i = 0; i < MAX_BALLS; i++) {
            if (!balls[i].active) continue;
            for (int k = 0; k < MAX_BALLS; k++) {
                if (balls[k].active) continue;
                balls[k] = balls[i];
                balls[k].vx = -balls[i].vx;
                balls[k].vy = -balls[i].vy;
                balls[k].active = true;
                break;
            }
            break;
        }
    } else if (type == PW_LASER) {
        laser_until = platform_tick_ms() + 8000;
        laser_ready = true;
    }
}

bool Breakout::ball_hits_brick(Ball& b, int row, int col) {
    Rect br{
        BRICK_LEFT + col * (BRICK_W + 4),
        BRICK_TOP + row * (BRICK_H + 4),
        BRICK_W, BRICK_H
    };
    return circle_rect_collide((int)b.x, (int)b.y, BALL_R, br);
}

void Breakout::on_brick_hit(Ball& b, int row, int col) {
    int& hp = brick_hp[row][col];
    if (hp == BR_STEEL) {
        // 钢砖不碎，只反弹
        return;
    }
    hp--;
    if (hp <= 0) {
        alive_bricks--;
        score += (BRICK_ROWS - row) * 10;
        uint32_t colors[3] = {0x00FF8040, 0x00FFD020, 0x00FFFFFF};
        particles.burst(b.x, b.y, 12, colors, 3, 2.0f);
        // 30% 概率掉道具
        if (rng.range(0, 99) < 30) {
            spawn_powerup(BRICK_LEFT + col * (BRICK_W + 4) + BRICK_W / 2,
                         BRICK_TOP + row * (BRICK_H + 4));
        }
        if (alive_bricks <= 0) {
            state = (level >= 5) ? BS_WIN : BS_LEVEL_CLEAR;
        }
    } else {
        score += 5;
    }
}

void Breakout::step_ball(Ball& b) {
    if (!b.active) return;
    b.x += b.vx;
    b.y += b.vy;

    // 左右墙
    if (b.x < BALL_R) { b.x = (float)BALL_R; b.vx = -b.vx; }
    else if (b.x > BW - BALL_R) { b.x = (float)(BW - BALL_R); b.vx = -b.vx; }
    // 顶墙
    if (b.y < BALL_R + 20) {
        b.y = (float)(BALL_R + 20);
        b.vy = -b.vy;
    }

    // 挡板
    if (b.vy > 0) {
        Rect pad{ paddle_x, PADDLE_Y, paddle_w, PADDLE_H };
        if (circle_rect_collide((int)b.x, (int)b.y, BALL_R, pad)) {
            b.y = (float)(PADDLE_Y - BALL_R - 1);
            float hit = (b.x - (paddle_x + paddle_w / 2.0f)) / (paddle_w / 2.0f);
            if (hit < -1) hit = -1;
            if (hit > 1) hit = 1;
            float speed = 3.0f + (float)level * 0.4f;
            b.vx = hit * speed * 0.8f;
            b.vy = -speed;
        }
    }

    // 砖块
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            if (brick_hp[r][c] == 0) continue;
            if (!ball_hits_brick(b, r, c)) continue;
            int bx = BRICK_LEFT + c * (BRICK_W + 4) + BRICK_W / 2;
            int by = BRICK_TOP + r * (BRICK_H + 4) + BRICK_H / 2;
            float dx = b.x - bx;
            float dy = b.y - by;
            if (dx * (BRICK_H / 2) > dy * (BRICK_W / 2)) b.vx = -b.vx;
            else b.vy = -b.vy;
            on_brick_hit(b, r, c);
            break;
        }
        if (state != BS_PLAY) return;
    }

    // 掉底
    if (b.y > BH + BALL_R) b.active = false;
}

void Breakout::step() {
    if (state != BS_PLAY) return;

    // 过期的宽挡板
    uint32_t now = platform_tick_ms();
    if (wide_until != 0 && now > wide_until) {
        paddle_w = PADDLE_W;
        wide_until = 0;
    }
    if (laser_until != 0 && now > laser_until) {
        laser_ready = false;
        laser_until = 0;
    }

    // 推进所有球
    bool any_active = false;
    for (int i = 0; i < MAX_BALLS; i++) {
        step_ball(balls[i]);
        if (balls[i].active) any_active = true;
    }
    if (!any_active) {
        lives--;
        if (lives <= 0) state = BS_GAME_OVER;
        else reset_ball_on_paddle();
    }

    // 道具下落
    for (int i = 0; i < 4; i++) {
        PowerUp& p = powers[i];
        if (!p.active) continue;
        p.y += POWER_FALL_SPEED;
        if (p.y > BH) { p.active = false; continue; }
        Rect pr{ (int)p.x, (int)p.y, POWER_W, POWER_H };
        Rect pad{ paddle_x, PADDLE_Y, paddle_w, PADDLE_H };
        if (rect_overlap(pr, pad)) {
            apply_powerup(p.type);
            p.active = false;
        }
    }

    particles.step();
}

// ===================== 渲染 =====================
// 每行颜色（顶部高分）
const uint32_t ROW_COLORS_LOCAL[BRICK_ROWS] = {
    0x00FF4040, 0x00FF8020, 0x00FFD020,
    0x0040D040, 0x004080FF, 0x00C040FF
};

static void breakout_paint(Window* w) {
    Breakout* g = (Breakout*)w->userdata;
    Surface& s = w->back;

    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, 0x000A0A14);

    gfx::fillrect(s, 0, 0, w->content_w, 20, 0x00181828);
    char buf[64];
    ksprintf(buf, sizeof(buf), "Score: %d", g->score);
    gfx::text(s, 8, 4, buf, 0x00FFFFFF, 0x00181828);
    ksprintf(buf, sizeof(buf), "Lives: %d", g->lives);
    gfx::text(s, 150, 4, buf, 0x00FFFFFF, 0x00181828);
    ksprintf(buf, sizeof(buf), "Level: %d", g->level);
    gfx::text(s, 260, 4, buf, 0x00FFFFFF, 0x00181828);
    if (g->laser_ready) gfx::text(s, 340, 4, "LASER", 0x00FF4040, 0x00181828);

    // 砖块
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            int hp = g->brick_hp[r][c];
            if (hp == 0) continue;
            int x = BRICK_LEFT + c * (BRICK_W + 4);
            int y = BRICK_TOP + r * (BRICK_H + 4);
            uint32_t col = (hp == BR_STEEL) ? 0x00808080
                          : (hp >= 3) ? 0x00FFD700
                          : (hp == 2) ? 0x00C0C0C0
                          : ROW_COLORS_LOCAL[r];
            gfx::fillrect(s, x, y, BRICK_W, BRICK_H, col);
            gfx::fillrect(s, x, y, BRICK_W, 2, 0x00FFFFFF);
            // 多血砖画裂纹
            if (hp == 2) gfx::fillrect(s, x + 10, y + 4, 2, BRICK_H - 8, 0x00000000);
        }
    }

    // 挡板
    gfx::fillrect(s, g->paddle_x, PADDLE_Y, g->paddle_w, PADDLE_H, 0x00FFFFFF);

    // 球
    for (int i = 0; i < MAX_BALLS; i++) {
        if (!g->balls[i].active) continue;
        gfx::fillcircle(s, (int)g->balls[i].x, (int)g->balls[i].y, BALL_R, 0x00FFFF40);
    }

    // 道具
    for (int i = 0; i < 4; i++) {
        if (!g->powers[i].active) continue;
        int x = (int)g->powers[i].x, y = (int)g->powers[i].y;
        uint32_t col = (g->powers[i].type == PW_WIDE) ? 0x0040FF40
                     : (g->powers[i].type == PW_MULTI) ? 0x00FF40FF
                     : 0x00FF4040;
        gfx::fillrect(s, x, y, POWER_W, POWER_H, col);
        char label[2] = {
            (g->powers[i].type == PW_WIDE) ? 'W'
          : (g->powers[i].type == PW_MULTI) ? 'M' : 'L', 0
        };
        gfx::text(s, x + 4, y + 1, label, 0x00FFFFFF, col);
    }

    g->particles.draw(s);

    // 覆盖层
    if (g->state == BS_READY) {
        draw_center_text(s, w->content_w, BH / 2, "Press SPACE to launch",
                         0x00FFFFFF, 0x00000000);
    } else if (g->state == BS_PAUSE) {
        draw_center_text(s, w->content_w, BH / 2, "PAUSED - press P",
                         0x00FFFF40, 0x00000000);
    } else if (g->state == BS_LEVEL_CLEAR) {
        draw_center_text(s, w->content_w, BH / 2 - 10, "LEVEL CLEAR!",
                         0x0040FF40, 0x00000000);
        draw_center_text(s, w->content_w, BH / 2 + 10, "Press SPACE for next",
                         0x00FFFFFF, 0x00000000);
    } else if (g->state == BS_GAME_OVER) {
        draw_center_text(s, w->content_w, BH / 2 - 10, "GAME OVER",
                         0x00FF4040, 0x00000000);
        draw_center_text(s, w->content_w, BH / 2 + 10, "Press R to restart",
                         0x00FFFFFF, 0x00000000);
    } else if (g->state == BS_WIN) {
        draw_center_text(s, w->content_w, BH / 2 - 10, "YOU WIN!",
                         0x00FFFF40, 0x00000000);
        ksprintf(buf, sizeof(buf), "Final score: %d", g->score);
        draw_center_text(s, w->content_w, BH / 2 + 10, buf,
                         0x00FFFFFF, 0x00000000);
    }
}

static void breakout_tick(Window* w) {
    Breakout* g = (Breakout*)w->userdata;
    if (!g) return;
    if (g->state == BS_PLAY || g->state == BS_READY) {
        if (g->key_left)  g->move_paddle(-PADDLE_SPEED);
        if (g->key_right) g->move_paddle(PADDLE_SPEED);
    }
    uint32_t now = platform_tick_ms();
    if (now - g->last_tick < 8) return;
    g->last_tick = now;
    g->step();
}

static void breakout_key(Window* w, const KeyEvent* e) {
    Breakout* g = (Breakout*)w->userdata;
    bool down = e->down;

    if (e->keycode == KEY_LEFT)  { g->key_left = down; return; }
    if (e->keycode == KEY_RIGHT) { g->key_right = down; return; }
    if (e->ascii == 'a' || e->ascii == 'A') { g->key_left = down; return; }
    if (e->ascii == 'd' || e->ascii == 'D') { g->key_right = down; return; }
    if (!down) return;

    if (e->ascii == ' ' || e->keycode == KEY_ENTER) {
        if (g->state == BS_READY) g->launch_ball();
        else if (g->state == BS_LEVEL_CLEAR) {
            g->level++;
            g->reset_bricks();
            g->reset_ball_on_paddle();
        } else if (g->state == BS_PLAY && g->laser_ready) {
            // 激光：从挡板中心向上发射，直接加 50 分（简化：销毁最近一列顶部砖块）
            for (int r = 0; r < BRICK_ROWS; r++) {
                int c = (g->paddle_x + g->paddle_w / 2 - BRICK_LEFT) / (BRICK_W + 4);
                if (c < 0) c = 0;
                if (c >= BRICK_COLS) c = BRICK_COLS - 1;
                if (g->brick_hp[r][c] != 0 && g->brick_hp[r][c] != BR_STEEL) {
                    g->brick_hp[r][c]--;
                    if (g->brick_hp[r][c] <= 0) g->alive_bricks--;
                    g->score += 10;
                    break;
                }
            }
        }
        return;
    }
    if (e->ascii == 'r' || e->ascii == 'R') { g->start_game(); return; }
    if (e->ascii == 'p' || e->ascii == 'P') {
        if (g->state == BS_PLAY) g->state = BS_PAUSE;
        else if (g->state == BS_PAUSE) g->state = BS_PLAY;
        return;
    }
}

static void breakout_close(Window* w) {
    if (w->userdata) delete (Breakout*)w->userdata;
    w->userdata = 0;
}

} // namespace

void breakout_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Breakout", x, y, BW, BH);
    if (!w) return;
    Breakout* g = new Breakout();
    g->start_game();
    g->last_tick = platform_tick_ms();
    w->userdata = g;
    w->on_paint = breakout_paint;
    w->on_tick = breakout_tick;
    w->on_key = breakout_key;
    w->on_close = breakout_close;
    g_wm->raise(w);
}

} // namespace nefu
