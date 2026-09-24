// spaceinv.cpp —— nefuOS 太空侵略者（Space Invaders）
//
// 玩法：
//   ←/→ 或 A/D    移动玩家飞船
//   空格          发射子弹（屏幕上最多 1 发）
//   R             重开
//
// 规则：
//   - 外星人分三种（顶行=鱿鱼 30 分、中行=螃蟹 20 分、底行=章鱼 10 分）
//   - 阵列整体左右移动，撞边后下移；清空后下一波加速
//   - 右上角偶尔出现 UFO，被击中随机得 50/100/150/300 分
//   - 护盾砖被子弹逐格侵蚀
//   - 玩家 3 命；外星人到底线直接游戏结束
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "games2_util.h"

namespace nefu {

namespace {

using namespace nefu::games2;

const int SW = 540;
const int SH = 480;

const int ALIEN_COLS = 10;
const int ALIEN_ROWS = 5;
const int ALIEN_W = 28;
const int ALIEN_H = 16;
const int ALIEN_GAP_X = 8;
const int ALIEN_GAP_Y = 10;
const int ALIEN_TOP = 50;
const int ALIEN_LEFT = 40;

const int PLAYER_W = 28;
const int PLAYER_H = 12;
const int PLAYER_Y = SH - 40;
const int PLAYER_SPEED = 4;

const int BULLET_W = 2;
const int BULLET_H = 8;
const int SHIELD_ROWS = 4;
const int SHIELD_COLS = 12;
const int SHIELD_GROUPS = 4;

const int UFO_W = 30;
const int UFO_H = 12;
const int UFO_SPEED = 2;

enum SIState {
    SI_READY = 0,
    SI_PLAY,
    SI_PLAYER_EXPLODE,   // 玩家爆炸动画中
    SI_GAMEOVER,
    SI_WAVE_CLEAR
};

struct Bullet {
    float x, y;
    bool active;
    bool from_player;
};

struct Popup {
    float x, y;
    int value;
    uint32_t until;
    bool active;
};

struct SpaceInv {
    uint8_t aliens[ALIEN_ROWS][ALIEN_COLS];
    int alive_count;
    float alien_x, alien_y;
    int alien_dir;
    uint32_t alien_step_timer;
    int alien_step_ms;
    int anim_frame;     // 外星人两帧动画切换

    float player_x;
    int lives;
    uint32_t explode_until;

    Bullet p_bullet;
    Bullet e_bullets[4];

    uint8_t shields[SHIELD_GROUPS][SHIELD_ROWS][SHIELD_COLS];
    int shield_y;

    // UFO
    float ufo_x;
    bool ufo_active;
    uint32_t ufo_next;

    Popup popups[4];
    ParticleSystem particles;

    int score;
    int wave;
    int state;
    bool key_left, key_right;
    uint32_t last_tick;
    Rng rng;

    void reset();
    void reset_wave();
    void fire_player();
    void fire_alien();
    void step();
    void move_aliens();
    void update_bullets();
    void spawn_popup(int x, int y, int value);
    Rect alien_rect(int row, int col) const;
    Rect shield_rect(int g, int r, int c) const;
    int alien_points(int row) const;
};

int SpaceInv::alien_points(int row) const {
    // 顶行 30，中行 20，底行 10
    if (row == 0) return 30;
    if (row <= 2) return 20;
    return 10;
}

void SpaceInv::reset() {
    score = 0;
    lives = 3;
    wave = 1;
    state = SI_PLAY;
    player_x = (SW - PLAYER_W) / 2.0f;
    key_left = key_right = false;
    rng.seed((uint32_t)(platform_tick_ms() & 0xFFFFu));
    p_bullet.active = false;
    for (int i = 0; i < 4; i++) e_bullets[i].active = false;
    for (int i = 0; i < 4; i++) popups[i].active = false;
    particles.reset();
    ufo_active = false;
    ufo_next = platform_tick_ms() + 8000;
    shield_y = PLAYER_Y - 60;
    explode_until = 0;
    anim_frame = 0;
    reset_wave();
}

void SpaceInv::reset_wave() {
    alive_count = 0;
    for (int r = 0; r < ALIEN_ROWS; r++) {
        for (int c = 0; c < ALIEN_COLS; c++) {
            aliens[r][c] = 1;
            alive_count++;
        }
    }
    alien_x = ALIEN_LEFT;
    alien_y = ALIEN_TOP;
    alien_dir = 1;
    alien_step_ms = 500 - wave * 40;
    if (alien_step_ms < 120) alien_step_ms = 120;
    alien_step_timer = platform_tick_ms();

    for (int g = 0; g < SHIELD_GROUPS; g++) {
        for (int r = 0; r < SHIELD_ROWS; r++) {
            for (int c = 0; c < SHIELD_COLS; c++) {
                if (r >= SHIELD_ROWS - 1 && (c >= 4 && c <= 7)) {
                    shields[g][r][c] = 0;
                } else {
                    shields[g][r][c] = 1;
                }
            }
        }
    }
    p_bullet.active = false;
    for (int i = 0; i < 4; i++) e_bullets[i].active = false;
}

Rect SpaceInv::alien_rect(int row, int col) const {
    Rect r;
    r.x = (int)(alien_x + col * (ALIEN_W + ALIEN_GAP_X));
    r.y = (int)(alien_y + row * (ALIEN_H + ALIEN_GAP_Y));
    r.w = ALIEN_W;
    r.h = ALIEN_H;
    return r;
}

Rect SpaceInv::shield_rect(int g, int r, int c) const {
    Rect rr;
    int group_w = SHIELD_COLS * 6;
    int total = SHIELD_GROUPS * group_w + (SHIELD_GROUPS - 1) * 20;
    int start_x = (SW - total) / 2;
    rr.x = start_x + g * (group_w + 20) + c * 6;
    rr.y = shield_y + r * 6;
    rr.w = 6;
    rr.h = 6;
    return rr;
}

void SpaceInv::fire_player() {
    if (p_bullet.active) return;
    p_bullet.x = player_x + PLAYER_W / 2 - BULLET_W / 2.0f;
    p_bullet.y = PLAYER_Y - BULLET_H;
    p_bullet.active = true;
    p_bullet.from_player = true;
}

void SpaceInv::fire_alien() {
    int col = rng.range(0, ALIEN_COLS - 1);
    for (int r = ALIEN_ROWS - 1; r >= 0; r--) {
        if (!aliens[r][col]) continue;
        Rect ar = alien_rect(r, col);
        for (int i = 0; i < 4; i++) {
            if (e_bullets[i].active) continue;
            e_bullets[i].x = ar.x + ALIEN_W / 2.0f;
            e_bullets[i].y = ar.y + ALIEN_H;
            e_bullets[i].active = true;
            e_bullets[i].from_player = false;
            return;
        }
        return;
    }
}

void SpaceInv::spawn_popup(int x, int y, int value) {
    for (int i = 0; i < 4; i++) {
        if (popups[i].active) continue;
        popups[i].x = (float)x;
        popups[i].y = (float)y;
        popups[i].value = value;
        popups[i].until = platform_tick_ms() + 800;
        popups[i].active = true;
        return;
    }
}

void SpaceInv::move_aliens() {
    float min_x = SW, max_x = 0, min_y = SH, max_y = 0;
    bool any = false;
    for (int r = 0; r < ALIEN_ROWS; r++) {
        for (int c = 0; c < ALIEN_COLS; c++) {
            if (!aliens[r][c]) continue;
            Rect ar = alien_rect(r, c);
            any = true;
            if (ar.x < min_x) min_x = (float)ar.x;
            if (ar.x + ar.w > max_x) max_x = (float)(ar.x + ar.w);
            if (ar.y < min_y) min_y = (float)ar.y;
            if (ar.y + ar.h > max_y) max_y = (float)(ar.y + ar.h);
        }
    }
    if (!any) return;

    float step = alien_dir * 8.0f;
    if ((alien_dir > 0 && max_x + step > SW - 10) ||
        (alien_dir < 0 && min_x + step < 10)) {
        alien_dir = -alien_dir;
        alien_y += 12;
    } else {
        alien_x += step;
    }
    anim_frame ^= 1;

    if (max_y > PLAYER_Y - 10) state = SI_GAMEOVER;
}

void SpaceInv::update_bullets() {
    // 玩家子弹
    if (p_bullet.active) {
        p_bullet.y -= 6.0f;
        if (p_bullet.y < 20) p_bullet.active = false;

        // 撞 UFO
        if (p_bullet.active && ufo_active) {
            Rect ur{ (int)ufo_x, ALIEN_TOP - 20, UFO_W, UFO_H };
            Rect br{ (int)p_bullet.x, (int)p_bullet.y, BULLET_W, BULLET_H };
            if (rect_overlap(ur, br)) {
                int pts = 50 * (rng.range(1, 6));
                score += pts;
                spawn_popup((int)ufo_x, ALIEN_TOP - 20, pts);
                uint32_t cols[3] = {0x00FF40FF, 0x00FFFF40, 0x00FFFFFF};
                particles.burst(ufo_x + UFO_W / 2, ALIEN_TOP - 14, 20, cols, 3, 2.5f);
                ufo_active = false;
                p_bullet.active = false;
            }
        }

        // 撞外星人
        for (int r = 0; r < ALIEN_ROWS && p_bullet.active; r++) {
            for (int c = 0; c < ALIEN_COLS; c++) {
                if (!aliens[r][c]) continue;
                Rect ar = alien_rect(r, c);
                Rect br{ (int)p_bullet.x, (int)p_bullet.y, BULLET_W, BULLET_H };
                if (rect_overlap(ar, br)) {
                    aliens[r][c] = 0;
                    alive_count--;
                    int pts = alien_points(r);
                    score += pts;
                    spawn_popup(ar.x, ar.y, pts);
                    uint32_t cols[2] = {0x005EB94D, 0x00FFFFFF};
                    particles.burst(ar.x + ALIEN_W / 2, ar.y + ALIEN_H / 2, 10, cols, 2, 1.8f);
                    p_bullet.active = false;
                    break;
                }
            }
        }
        // 撞护盾
        if (p_bullet.active) {
            for (int g = 0; g < SHIELD_GROUPS && p_bullet.active; g++) {
                for (int r = 0; r < SHIELD_ROWS && p_bullet.active; r++) {
                    for (int c = 0; c < SHIELD_COLS; c++) {
                        if (!shields[g][r][c]) continue;
                        Rect sr = shield_rect(g, r, c);
                        Rect br{ (int)p_bullet.x, (int)p_bullet.y, BULLET_W, BULLET_H };
                        if (rect_overlap(sr, br)) {
                            shields[g][r][c] = 0;
                            p_bullet.active = false;
                            break;
                        }
                    }
                }
            }
        }
    }

    // 外星人子弹
    for (int i = 0; i < 4; i++) {
        Bullet& b = e_bullets[i];
        if (!b.active) continue;
        b.y += 3.0f;
        if (b.y > SH) { b.active = false; continue; }

        Rect pr{ (int)player_x, PLAYER_Y, PLAYER_W, PLAYER_H };
        Rect br{ (int)b.x, (int)b.y, BULLET_W, BULLET_H };
        if (rect_overlap(pr, br)) {
            b.active = false;
            lives--;
            uint32_t cols[3] = {0x00FFFFFF, 0x00FF8020, 0x00FF4040};
            particles.burst(player_x + PLAYER_W / 2, PLAYER_Y, 24, cols, 3, 2.5f);
            if (lives <= 0) state = SI_GAMEOVER;
            else {
                state = SI_PLAYER_EXPLODE;
                explode_until = platform_tick_ms() + 800;
            }
            continue;
        }
        for (int g = 0; g < SHIELD_GROUPS && b.active; g++) {
            for (int r = 0; r < SHIELD_ROWS && b.active; r++) {
                for (int c = 0; c < SHIELD_COLS; c++) {
                    if (!shields[g][r][c]) continue;
                    Rect sr = shield_rect(g, r, c);
                    if (rect_overlap(sr, br)) {
                        shields[g][r][c] = 0;
                        b.active = false;
                        break;
                    }
                }
            }
        }
    }
}

void SpaceInv::step() {
    if (state == SI_PLAYER_EXPLODE) {
        if (platform_tick_ms() > explode_until) {
            state = SI_PLAY;
            player_x = (SW - PLAYER_W) / 2.0f;
        }
        return;
    }
    if (state != SI_PLAY) return;

    if (key_left)  player_x -= PLAYER_SPEED;
    if (key_right) player_x += PLAYER_SPEED;
    if (player_x < 0) player_x = 0;
    if (player_x > SW - PLAYER_W) player_x = (float)(SW - PLAYER_W);

    uint32_t now = platform_tick_ms();
    if (now - alien_step_timer >= (uint32_t)alien_step_ms) {
        alien_step_timer = now;
        move_aliens();
        if (state != SI_PLAY) return;
        if (rng.range(0, 100) < 25) fire_alien();
    }

    // UFO 生成与移动
    if (!ufo_active && now > ufo_next) {
        ufo_active = true;
        ufo_x = (rng.chance() ? -UFO_W : (float)SW);
    }
    if (ufo_active) {
        ufo_x += (ufo_x < SW / 2) ? UFO_SPEED : -UFO_SPEED;
        if (ufo_x < -UFO_W - 10 || ufo_x > SW + 10) {
            ufo_active = false;
            ufo_next = now + 10000 + rng.range(0, 8000);
        }
    }

    update_bullets();
    particles.step();

    // 弹出标记过期
    for (int i = 0; i < 4; i++) {
        if (popups[i].active && now > popups[i].until) popups[i].active = false;
    }

    if (alive_count <= 0) {
        wave++;
        state = SI_WAVE_CLEAR;
    }
}

// ===================== 渲染 =====================
static void si_paint(Window* w) {
    SpaceInv* g = (SpaceInv*)w->userdata;
    Surface& s = w->back;

    gfx::fillrect(s, 0, 0, SW, SH, 0x00000000);

    char buf[48];
    ksprintf(buf, sizeof(buf), "Score: %d   Lives: %d   Wave: %d",
            g->score, g->lives, g->wave);
    gfx::text(s, 8, 6, buf, 0x00FFFFFF, 0x00000000);

    // UFO
    if (g->ufo_active) {
        gfx::fillrect(s, (int)g->ufo_x, ALIEN_TOP - 20, UFO_W, 6, 0x00FF40FF);
        gfx::fillrect(s, (int)g->ufo_x + 5, ALIEN_TOP - 26, UFO_W - 10, 6, 0x00FF40FF);
    }

    // 外星人：按行着色，两帧动画（交替宽度）
    for (int r = 0; r < ALIEN_ROWS; r++) {
        for (int c = 0; c < ALIEN_COLS; c++) {
            if (!g->aliens[r][c]) continue;
            Rect ar = g->alien_rect(r, c);
            uint32_t col;
            if (r == 0) col = 0x00FF60FF;            // 鱿鱼
            else if (r <= 2) col = 0x005EB94D;       // 螃蟹
            else col = 0x0060C0FF;                  // 章鱼
            int inset = g->anim_frame ? 0 : 2;
            gfx::fillrect(s, ar.x + inset, ar.y, ar.w - inset * 2, ar.h, col);
            gfx::fillrect(s, ar.x + 5, ar.y + 4, 3, 3, 0x00000000);
            gfx::fillrect(s, ar.x + ar.w - 8, ar.y + 4, 3, 3, 0x00000000);
        }
    }

    // 护盾
    for (int grp = 0; grp < SHIELD_GROUPS; grp++) {
        for (int r = 0; r < SHIELD_ROWS; r++) {
            for (int c = 0; c < SHIELD_COLS; c++) {
                if (!g->shields[grp][r][c]) continue;
                Rect sr = g->shield_rect(grp, r, c);
                gfx::fillrect(s, sr.x, sr.y, sr.w, sr.h, 0x0040C040);
            }
        }
    }

    // 玩家飞船（爆炸动画期间不画）
    if (g->state != SI_PLAYER_EXPLODE) {
        gfx::fillrect(s, (int)g->player_x, PLAYER_Y, PLAYER_W, PLAYER_H, 0x00FFFFFF);
        gfx::fillrect(s, (int)g->player_x + PLAYER_W / 2 - 3, PLAYER_Y - 6, 6, 6, 0x00FFFFFF);
    }

    // 子弹
    if (g->p_bullet.active) {
        gfx::fillrect(s, (int)g->p_bullet.x, (int)g->p_bullet.y,
                      BULLET_W, BULLET_H, 0x00FFFF40);
    }
    for (int i = 0; i < 4; i++) {
        if (!g->e_bullets[i].active) continue;
        gfx::fillrect(s, (int)g->e_bullets[i].x, (int)g->e_bullets[i].y,
                      BULLET_W, BULLET_H, 0x00FF6060);
    }

    g->particles.draw(s);

    // 得分弹出
    for (int i = 0; i < 4; i++) {
        if (!g->popups[i].active) continue;
        char pb[16];
        ksprintf(pb, sizeof(pb), "%d", g->popups[i].value);
        gfx::text(s, (int)g->popups[i].x, (int)g->popups[i].y, pb,
                  0x00FFFF40, 0x00000000);
    }

    if (g->state == SI_GAMEOVER) {
        gfx::fillrect(s, 0, SH / 2 - 20, SW, 50, 0x00000000);
        draw_center_text(s, SW, SH / 2 - 10, "GAME OVER", 0x00FF4040, 0x00000000);
        draw_center_text(s, SW, SH / 2 + 10, "Press R", 0x00FFFFFF, 0x00000000);
    } else if (g->state == SI_WAVE_CLEAR) {
        gfx::fillrect(s, 0, SH / 2 - 20, SW, 50, 0x00000000);
        draw_center_text(s, SW, SH / 2 - 10, "WAVE CLEAR!", 0x00FFFF40, 0x00000000);
        draw_center_text(s, SW, SH / 2 + 10, "Press SPACE", 0x00FFFFFF, 0x00000000);
    }
}

static void si_tick(Window* w) {
    SpaceInv* g = (SpaceInv*)w->userdata;
    uint32_t now = platform_tick_ms();
    if (now - g->last_tick < 16) return;
    g->last_tick = now;
    g->step();
}

static void si_key(Window* w, const KeyEvent* e) {
    SpaceInv* g = (SpaceInv*)w->userdata;
    bool down = e->down;

    if (e->keycode == KEY_LEFT)  { g->key_left = down; return; }
    if (e->keycode == KEY_RIGHT) { g->key_right = down; return; }
    if (e->ascii == 'a' || e->ascii == 'A') { g->key_left = down; return; }
    if (e->ascii == 'd' || e->ascii == 'D') { g->key_right = down; return; }

    if (!down) return;
    if (e->ascii == ' ' || e->keycode == KEY_ENTER) {
        if (g->state == SI_PLAY) g->fire_player();
        else if (g->state == SI_WAVE_CLEAR) { g->reset_wave(); g->state = SI_PLAY; }
        return;
    }
    if (e->ascii == 'r' || e->ascii == 'R') { g->reset(); return; }
}

static void si_close(Window* w) {
    if (w->userdata) delete (SpaceInv*)w->userdata;
    w->userdata = 0;
}

} // namespace

void spaceinv_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Space Invaders", x, y, SW, SH);
    if (!w) return;
    SpaceInv* g = new SpaceInv();
    g->reset();
    g->last_tick = platform_tick_ms();
    w->userdata = g;
    w->on_paint = si_paint;
    w->on_tick = si_tick;
    w->on_key = si_key;
    w->on_close = si_close;
    g_wm->raise(w);
}

} // namespace nefu
