// pacman.cpp —— nefuOS 吃豆人（Pac-Man 简化版）
//
// 玩法：
//   ←/↑/↓/→ 或 WASD   转向（会在可行时在路口转弯）
//   R                 重开
//
// 规则：
//   - 吃普通豆 +10，能量豆 +50 并让幽灵变蓝 5 秒
//   - 蓝幽灵可被吃：第一个 +200，第二个 +400，第三个 +800，第四个 +1600
//   - 非蓝幽灵碰到玩家损失 1 命（共 3 命）
//   - 吃完所有豆子过关
//
// 幽灵 AI：
//   - 三个幽灵有名字（Blinky 红 / Pinky 粉 / Inky 青），颜色区分
//   - 追逐模式：朝玩家方向走；散布模式：朝各自角落走
//   - 模式每 7 秒在追逐/散布之间切换
//   - 能量豆期间反向逃跑
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "games2_util.h"

namespace nefu {

namespace {

using namespace nefu::games2;

inline int iabs(int v) { return v < 0 ? -v : v; }

const int MAZE_W = 19;
const int MAZE_H = 21;
const int TILE = 18;
const int PW = MAZE_W * TILE;
const int PH = MAZE_H * TILE + 24;

const char* MAZE[MAZE_H] = {
    "###################",
    "#........#........#",
    "#o##.###.#.###.##o#",
    "#.................#",
    "#.##.#.#####.#.##.#",
    "#....#...#...#....#",
    "####.###.#.###.####",
    "####.#.......#.####",
    "####.#.##-##.#.####",
    ".....#.#GGG#.#.....",
    "####.#.#####.#.####",
    "####.#.......#.####",
    "####.#.#####.#.####",
    "#........#........#",
    "#.##.###.#.###.##.#",
    "#o.#.....P.....#.o#",
    "##.#.#.#####.#.#.##",
    "#....#...#...#....#",
    "#.######.#.######.#",
    "#.................#",
    "###################"
};

enum PacDir { DIR_NONE = 0, DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };

// 幽灵模式
enum GhostMode { GM_CHASE = 0, GM_SCATTER = 1 };

struct Actor {
    int x, y;
    int dir;
    int want_dir;
};

struct PacGame {
    uint8_t walls[MAZE_H][MAZE_W];
    uint8_t pellets[MAZE_H][MAZE_W];
    uint8_t power[MAZE_H][MAZE_W];
    int remaining_pellets;

    Actor pac;
    Actor ghosts[3];
    uint32_t fright_until;
    uint32_t mode_switch_at;
    GhostMode mode;
    int ghost_combo;        // 一次能量豆期间连吃的幽灵数

    int score;
    int lives;
    int state;
    uint32_t last_step;
    Rng rng;

    void reset();
    bool walkable(int x, int y) const;
    void set_want(PacDir d);
    void step_pac();
    void step_ghost(int i);
    void eat_at();
    bool ghost_eaten_check();
    void respawn_actors();
    void update_mode(uint32_t now);
};

bool PacGame::walkable(int x, int y) const {
    if (x < 0 || x >= MAZE_W || y < 0 || y >= MAZE_H) return false;
    return walls[y][x] == 0;
}

void PacGame::reset() {
    remaining_pellets = 0;
    int pac_x = 9, pac_y = 15;
    for (int y = 0; y < MAZE_H; y++) {
        for (int x = 0; x < MAZE_W; x++) {
            char c = MAZE[y][x];
            walls[y][x] = (c == '#') ? 1 : 0;
            pellets[y][x] = (c == '.') ? 1 : 0;
            power[y][x] = (c == 'o') ? 1 : 0;
            if (c == 'P') { pac_x = x; pac_y = y; }
            if (pellets[y][x] || power[y][x]) remaining_pellets++;
        }
    }
    pac.x = pac_x; pac.y = pac_y;
    pac.dir = DIR_NONE; pac.want_dir = DIR_NONE;
    ghosts[0].x = 8; ghosts[0].y = 9;
    ghosts[1].x = 9; ghosts[1].y = 9;
    ghosts[2].x = 10; ghosts[2].y = 9;
    for (int i = 0; i < 3; i++) {
        ghosts[i].dir = DIR_NONE;
        ghosts[i].want_dir = DIR_LEFT;
    }
    score = 0;
    lives = 3;
    state = 0;
    fright_until = 0;
    mode = GM_SCATTER;
    mode_switch_at = platform_tick_ms() + 7000;
    ghost_combo = 0;
    rng.seed((uint32_t)(platform_tick_ms() & 0xFFFFu));
    last_step = platform_tick_ms();
}

void PacGame::set_want(PacDir d) { pac.want_dir = d; }

static void next_pos(int x, int y, int dir, int& nx, int& ny) {
    nx = x; ny = y;
    switch (dir) {
    case DIR_UP:    ny--; break;
    case DIR_DOWN:  ny++; break;
    case DIR_LEFT:  nx--; break;
    case DIR_RIGHT: nx++; break;
    default: break;
    }
}

void PacGame::step_pac() {
    int nx, ny;
    next_pos(pac.x, pac.y, pac.want_dir, nx, ny);
    if (pac.want_dir != DIR_NONE && walkable(nx, ny)) {
        pac.dir = pac.want_dir;
    }
    next_pos(pac.x, pac.y, pac.dir, nx, ny);
    if (walkable(nx, ny)) { pac.x = nx; pac.y = ny; }
    else pac.dir = DIR_NONE;
    eat_at();
}

void PacGame::eat_at() {
    if (pellets[pac.y][pac.x]) {
        pellets[pac.y][pac.x] = 0;
        score += 10;
        remaining_pellets--;
    } else if (power[pac.y][pac.x]) {
        power[pac.y][pac.x] = 0;
        score += 50;
        remaining_pellets--;
        fright_until = platform_tick_ms() + 5000;
        ghost_combo = 0;
    }
    if (remaining_pellets <= 0) state = 2;
}

void PacGame::update_mode(uint32_t now) {
    if (now > mode_switch_at) {
        mode = (mode == GM_CHASE) ? GM_SCATTER : GM_CHASE;
        mode_switch_at = now + 7000;
    }
}

void PacGame::step_ghost(int i) {
    Actor& g = ghosts[i];
    bool frightened = platform_tick_ms() < fright_until;

    // 散布模式的目标角落
    int tx, ty;
    if (mode == GM_SCATTER) {
        static const int corners[3][2] = { {1,1}, {MAZE_W-2,1}, {MAZE_W-2,MAZE_H-2} };
        tx = corners[i][0]; ty = corners[i][1];
    } else if (frightened) {
        // 逃跑时目标点随机（评分会选离玩家最远的方向）
        tx = rng.range(1, MAZE_W - 2);
        ty = rng.range(1, MAZE_H - 2);
    } else {
        // 追逐模式：三个幽灵个性
        // i=0 Blinky 红：直接追玩家
        // i=1 Pinky 粉：瞄准玩家前方 4 格
        // i=2 Inky 青：以 Blinky 为基，玩家前方 2 格的向量镜像
        if (i == 0) {
            tx = pac.x; ty = pac.y;
        } else if (i == 1) {
            tx = pac.x; ty = pac.y;
            switch (pac.dir) {
            case DIR_UP:    ty -= 4; break;
            case DIR_DOWN:  ty += 4; break;
            case DIR_LEFT:  tx -= 4; break;
            case DIR_RIGHT: tx += 4; break;
            default: break;
            }
        } else {
            int bx = ghosts[0].x, by = ghosts[0].y;
            int ax = pac.x, ay = pac.y;
            switch (pac.dir) {
            case DIR_UP:    ay -= 2; break;
            case DIR_DOWN:  ay += 2; break;
            case DIR_LEFT:  ax -= 2; break;
            case DIR_RIGHT: ax += 2; break;
            default: break;
            }
            tx = ax * 2 - bx;
            ty = ay * 2 - by;
        }
    }

    int dirs[4] = { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };
    int best_dir = g.dir;
    int best_score = frightened ? -1000000 : 1000000;

    int reverse = 0;
    switch (g.dir) {
    case DIR_UP:    reverse = DIR_DOWN; break;
    case DIR_DOWN:  reverse = DIR_UP; break;
    case DIR_LEFT:  reverse = DIR_RIGHT; break;
    case DIR_RIGHT: reverse = DIR_LEFT; break;
    default: break;
    }

    for (int k = 0; k < 4; k++) {
        int d = dirs[k];
        if (g.dir != DIR_NONE && d == reverse) continue;
        int nx, ny;
        next_pos(g.x, g.y, d, nx, ny);
        if (!walkable(nx, ny)) continue;
        int dist = iabs(nx - tx) + iabs(ny - ty);
        int s = frightened ? dist : -dist;
        s += rng.range(-3, 3);
        if (s > best_score) { best_score = s; best_dir = d; }
    }

    g.dir = best_dir;
    int nx, ny;
    next_pos(g.x, g.y, g.dir, nx, ny);
    if (walkable(nx, ny)) { g.x = nx; g.y = ny; }
}

bool PacGame::ghost_eaten_check() {
    bool frightened = platform_tick_ms() < fright_until;
    for (int i = 0; i < 3; i++) {
        if (ghosts[i].x != pac.x || ghosts[i].y != pac.y) continue;
        if (frightened) {
            // 连击分：200/400/800/1600
            static const int COMBO[4] = { 200, 400, 800, 1600 };
            int idx = ghost_combo < 4 ? ghost_combo : 3;
            score += COMBO[idx];
            ghost_combo++;
            ghosts[i].x = 9; ghosts[i].y = 9;
            ghosts[i].dir = DIR_NONE;
        } else {
            lives--;
            if (lives <= 0) state = 1;
            else respawn_actors();
            return true;
        }
    }
    return false;
}

void PacGame::respawn_actors() {
    pac.x = 9; pac.y = 15;
    pac.dir = DIR_NONE; pac.want_dir = DIR_NONE;
    ghosts[0].x = 8; ghosts[0].y = 9;
    ghosts[1].x = 9; ghosts[1].y = 9;
    ghosts[2].x = 10; ghosts[2].y = 9;
    ghosts[0].dir = ghosts[1].dir = ghosts[2].dir = DIR_NONE;
    ghost_combo = 0;
}

// ===================== 渲染 =====================
static void pac_paint(Window* w) {
    PacGame* g = (PacGame*)w->userdata;
    Surface& s = w->back;

    gfx::fillrect(s, 0, 0, PW, PH, 0x00000020);

    for (int y = 0; y < MAZE_H; y++) {
        for (int x = 0; x < MAZE_W; x++) {
            int px = x * TILE;
            int py = y * TILE;
            if (g->walls[y][x]) {
                gfx::fillrect(s, px, py, TILE, TILE, 0x002020C0);
            } else {
                if (g->pellets[y][x]) {
                    gfx::fillcircle(s, px + TILE / 2, py + TILE / 2, 2, 0x00FFE0B0);
                } else if (g->power[y][x]) {
                    gfx::fillcircle(s, px + TILE / 2, py + TILE / 2, 5, 0x00FFE0B0);
                }
            }
        }
    }

    int px = g->pac.x * TILE + TILE / 2;
    int py = g->pac.y * TILE + TILE / 2;
    gfx::fillcircle(s, px, py, TILE / 2 - 2, 0x00FFE040);

    bool frightened = platform_tick_ms() < g->fright_until;
    // 三个幽灵：Blinky 红 / Pinky 粉 / Inky 青
    static const uint32_t GHOST_COLORS[3] = {
        0x00FF4040, 0x00FFB0FF, 0x0040FFFF
    };
    for (int i = 0; i < 3; i++) {
        int gx = g->ghosts[i].x * TILE + TILE / 2;
        int gy = g->ghosts[i].y * TILE + TILE / 2;
        uint32_t col = frightened ? 0x003030FF : GHOST_COLORS[i];
        gfx::fillcircle(s, gx, gy, TILE / 2 - 2, col);
        gfx::fillrect(s, gx - TILE / 2 + 2, gy, TILE - 4, TILE / 2 - 2, col);
    }

    char buf[80];
    ksprintf(buf, sizeof(buf), "Score: %d   Lives: %d   Left: %d   %s",
            g->score, g->lives, g->remaining_pellets,
            frightened ? "POWER!" : (g->mode == GM_CHASE ? "CHASE" : "SCATTER"));
    gfx::fillrect(s, 0, MAZE_H * TILE, PW, 24, 0x00000000);
    gfx::text(s, 6, MAZE_H * TILE + 6, buf, 0x00FFFFFF, 0x00000000);

    if (g->state == 1) {
        draw_center_text(s, PW, PH / 2 - 10, "GAME OVER", 0x00FF4040, 0x00000000);
        draw_center_text(s, PW, PH / 2 + 10, "Press R", 0x00FFFFFF, 0x00000000);
    } else if (g->state == 2) {
        draw_center_text(s, PW, PH / 2 - 10, "YOU WIN!", 0x00FFFF40, 0x00000000);
        draw_center_text(s, PW, PH / 2 + 10, "Press R", 0x00FFFFFF, 0x00000000);
    }
}

static void pac_tick(Window* w) {
    PacGame* g = (PacGame*)w->userdata;
    if (g->state != 0) return;
    uint32_t now = platform_tick_ms();
    if (now - g->last_step < 160) return;
    g->last_step = now;

    g->update_mode(now);
    g->step_pac();
    if (g->state != 0) return;
    for (int i = 0; i < 3; i++) g->step_ghost(i);
    g->ghost_eaten_check();
}

static void pac_key(Window* w, const KeyEvent* e) {
    PacGame* g = (PacGame*)w->userdata;
    if (!e->down) return;
    switch (e->keycode) {
    case KEY_UP:    g->set_want(DIR_UP);    return;
    case KEY_DOWN:  g->set_want(DIR_DOWN);  return;
    case KEY_LEFT:  g->set_want(DIR_LEFT); return;
    case KEY_RIGHT: g->set_want(DIR_RIGHT);return;
    default: break;
    }
    switch (e->ascii) {
    case 'w': case 'W': g->set_want(DIR_UP);    return;
    case 's': case 'S': g->set_want(DIR_DOWN);  return;
    case 'a': case 'A': g->set_want(DIR_LEFT);  return;
    case 'd': case 'D': g->set_want(DIR_RIGHT); return;
    }
    if (e->ascii == 'r' || e->ascii == 'R') g->reset();
}

static void pac_close(Window* w) {
    if (w->userdata) delete (PacGame*)w->userdata;
    w->userdata = 0;
}

} // namespace

void pacman_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Pac-Man", x, y, PW, PH);
    if (!w) return;
    PacGame* g = new PacGame();
    g->reset();
    w->userdata = g;
    w->on_paint = pac_paint;
    w->on_tick = pac_tick;
    w->on_key = pac_key;
    w->on_close = pac_close;
    g_wm->raise(w);
}

} // namespace nefu
