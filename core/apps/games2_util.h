// games2_util.h —— nefuOS 第二批小游戏的公共工具库
//
// 本头文件提供：
//   1. Rng        —— 32 位 xorshift 随机数生成器（可播种，便于单元测试复现）
//   2. Rect/碰撞  —— AABB 矩形重叠、点在矩形内、圆与矩形碰撞检测
//   3. HighTable  —— 内存高分表（top-N），跨游戏共享
//   4. 精灵绘制   —— 8x8 / 16x16 位图精灵直接画到离屏 Surface 上（inline）
//
// 设计约束：
//   - 不使用 STL 容器、不抛异常、不使用 RTTI
//   - 纯逻辑部分（Rng / 碰撞 / HighTable）放在 games2_util.cpp，
//     使得 tests/games2_test_main.cpp 可以脱离 gfxlib 单独链接；
//   - 依赖 Surface 的精灵绘制函数在此头文件中 inline，未被调用时不产生符号。
#pragma once

#include "../gui/gfx.h"

namespace nefu {
namespace games2 {

// ============================================================================
//  随机数：xorshift32
//  周期 2^32-1，速度极快，适合游戏中需要的随机抖动/洗牌/掉落位置
// ============================================================================
struct Rng {
    uint32_t state;

    // 构造时若传入 0，自动换成一个非零种子，避免卡死在全零状态
    Rng(uint32_t seed = 0x12345678u)
        : state(seed ? seed : 0x9E3779B9u) {}

    // 重新播种
    void seed(uint32_t s) { state = s ? s : 0x9E3779B9u; }

    // 取下一个 32 位随机数
    uint32_t next();

    // 取 [lo, hi] 闭区间内的均匀整数（hi >= lo）
    int range(int lo, int hi);

    // 50% 概率返回 true
    bool chance() { return (next() & 1u) == 0; }
};

// ============================================================================
//  几何与碰撞
// ============================================================================
struct Rect {
    int x, y, w, h;
};

// 两个 AABB 矩形是否相交（含边）
bool rect_overlap(const Rect& a, const Rect& b);

// 便捷重载：直接传坐标
bool rect_overlap_xy(int ax, int ay, int aw, int ah,
                     int bx, int by, int bw, int bh);

// 点是否落在矩形内（含边界）
bool point_in_rect(int px, int py, const Rect& r);

// 圆（中心 cx,cy，半径 cr）与轴对齐矩形是否相交
// 用于：球 vs 挡板 / 球 vs 砖块 / 鸟 vs 管道
bool circle_rect_collide(int cx, int cy, int cr, const Rect& r);

// 圆与圆相交
bool circle_circle_collide(int ax, int ay, int ar,
                           int bx, int by, int br);

// ============================================================================
//  高分表：进程内保存 top-N
//  每个游戏提交自己的分数，按分数降序排列。
//  这里不做磁盘持久化（内核态无标准 fopen 依赖），
//  后续如需持久化，可在 submit 时回调 vfs 写入。
// ============================================================================
const int HIGH_SCORE_SLOTS = 8;

struct HighTable {
    char names[HIGH_SCORE_SLOTS][16];
    int  scores[HIGH_SCORE_SLOTS];
    int  count;

    HighTable();

    // 把 (name, score) 插入高分表，返回排名（0 起）；未上榜返回 -1
    int submit(const char* name, int score);

    // 清空
    void reset();
};

// 全局共享高分表（在 games2_util.cpp 中定义）
extern HighTable g_high;

// ============================================================================
//  精灵绘制（inline，仅在游戏调用时才编入）
//  rows[8] 的每一位对应一个像素：bit7 = 最左列，bit0 = 最右列
// ============================================================================
inline void draw_sprite8(Surface& s, int x, int y,
                         const uint8_t rows[8], uint32_t fg) {
    for (int r = 0; r < 8; r++) {
        uint8_t bits = rows[r];
        for (int c = 0; c < 8; c++) {
            if (bits & (0x80 >> c)) {
                s.setpx(x + c, y + r, fg);
            }
        }
    }
}

// 16x16 精灵（双字节行版）：每行 rows[r][0] 为左 8 列，rows[r][1] 为右 8 列
inline void draw_sprite16x2(Surface& s, int x, int y,
                            const uint8_t rows[16][2], uint32_t fg) {
    for (int r = 0; r < 16; r++) {
        for (int half = 0; half < 2; half++) {
            uint8_t bits = rows[r][half];
            for (int c = 0; c < 8; c++) {
                if (bits & (0x80 >> c)) {
                    s.setpx(x + half * 8 + c, y + r, fg);
                }
            }
        }
    }
}

// 在 Surface 水平居中绘制一行文字
inline void draw_center_text(Surface& s, int w, int y,
                            const char* str, uint32_t fg, uint32_t bg) {
    int tw = gfx::text_width(str);
    int x = (w - tw) / 2;
    if (x < 0) x = 0;
    gfx::text(s, x, y, str, fg, bg);
}

// ============================================================================
//  粒子系统：固定容量，用于爆炸/得分飘字
//  不使用 STL，粒子池上限 PARTICLE_MAX
// ============================================================================
const int PARTICLE_MAX = 128;

struct Particle {
    float x, y;
    float vx, vy;
    int   life;       // 剩余帧数
    int   max_life;
    uint32_t color;
    int   size;
    bool  active;
};

struct ParticleSystem {
    Particle pool[PARTICLE_MAX];

    ParticleSystem() { reset(); }

    void reset() {
        for (int i = 0; i < PARTICLE_MAX; i++) pool[i].active = false;
    }

    // 在 (x,y) 爆发 n 个粒子，颜色从 colors 数组中循环取
    void burst(float x, float y, int n, const uint32_t colors[], int ncolors,
               float speed = 2.0f) {
        // 预计算的 16 方向单位圆上的偏移（避免三角函数依赖）
        static const float DIRS[16][2] = {
            {1.0f,0.0f},{0.92f,0.38f},{0.71f,0.71f},{0.38f,0.92f},
            {0.0f,1.0f},{-0.38f,0.92f},{-0.71f,0.71f},{-0.92f,0.38f},
            {-1.0f,0.0f},{-0.92f,-0.38f},{-0.71f,-0.71f},{-0.38f,-0.92f},
            {0.0f,-1.0f},{0.38f,-0.92f},{0.71f,-0.71f},{0.92f,-0.38f}
        };
        int spawned = 0;
        for (int i = 0; i < PARTICLE_MAX && spawned < n; i++) {
            if (pool[i].active) continue;
            Particle& p = pool[i];
            p.x = x; p.y = y;
            int d = spawned % 16;
            float sp = speed * (0.5f + (float)(spawned % 5) / 5.0f);
            p.vx = DIRS[d][0] * sp;
            p.vy = DIRS[d][1] * sp;
            p.max_life = 20 + spawned % 15;
            p.life = p.max_life;
            p.color = colors[spawned % ncolors];
            p.size = 2 + spawned % 2;
            p.active = true;
            spawned++;
        }
    }

    // 每帧推进
    void step() {
        for (int i = 0; i < PARTICLE_MAX; i++) {
            Particle& p = pool[i];
            if (!p.active) continue;
            p.x += p.vx;
            p.y += p.vy;
            p.vy += 0.05f;   // 重力
            p.life--;
            if (p.life <= 0) p.active = false;
        }
    }

    // 渲染
    void draw(Surface& s) {
        for (int i = 0; i < PARTICLE_MAX; i++) {
            Particle& p = pool[i];
            if (!p.active) continue;
            int a = (p.life * 255) / p.max_life;
            (void)a;
            gfx::fillrect(s, (int)p.x, (int)p.y, p.size, p.size, p.color);
        }
    }
};

// ============================================================================
//  FPS / 帧计数工具
// ============================================================================
struct FpsMeter {
    uint32_t start;
    int frames;
    int fps;

    FpsMeter() : start(0), frames(0), fps(0) {}

    void tick(uint32_t now_ms) {
        if (start == 0) start = now_ms;
        frames++;
        if (now_ms - start >= 1000) {
            fps = frames * 1000 / (int)(now_ms - start);
            start = now_ms;
            frames = 0;
        }
    }
};

// 简单的 4x6 ASCII 数字字体（用于无 gfx::text 依赖的纯数字绘制）
// 这里仅做一个矩形边框 + 填充的辅助，实际字体由 gfx::text 负责。
inline void draw_panel(Surface& s, int x, int y, int w, int h,
                      uint32_t bg, uint32_t border) {
    gfx::fillrect(s, x, y, w, h, bg);
    gfx::rect(s, x, y, w, h, border);
}

// ============================================================================
//  数学辅助（内联，无依赖）
// ============================================================================
inline int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
inline float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
inline float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

// ============================================================================
//  精灵库：8x8 位图，供各游戏复用
//  每帧 8 字节，bit7=最左列
// ============================================================================

// 小飞船（向下指）
const uint8_t SPR_SHIP[8] = {
    0b00011000, 0b00111100, 0b01111110, 0b11011011,
    0b11111111, 0b00100100, 0b01000010, 0b10000001
};

// 外星人（帧 A）
const uint8_t SPR_ALIEN_A[8] = {
    0b00011000, 0b00111100, 0b01111110, 0b11011011,
    0b11111111, 0b00100100, 0b01000010, 0b10000001
};

// 外星人（帧 B：触角上翘）
const uint8_t SPR_ALIEN_B[8] = {
    0b10000001, 0b00011000, 0b00111100, 0b01111110,
    0b11011011, 0b11111111, 0b00100100, 0b01000010
};

// 幽灵（正面）
const uint8_t SPR_GHOST[8] = {
    0b01111110, 0b11111111, 0b11011011, 0b11111111,
    0b11111111, 0b11111111, 0b11011011, 0b10100101
};

// 吃豆人（张嘴朝右）
const uint8_t SPR_PACMAN[8] = {
    0b00111100, 0b01110000, 0b11100000, 0b11100111,
    0b11100111, 0b11100000, 0b01110000, 0b00111100
};

// 雷
const uint8_t SPR_MINE[8] = {
    0b00011000, 0b00111100, 0b01011010, 0b11111111,
    0b11111111, 0b01011010, 0b00111100, 0b00011000
};

// 旗子
const uint8_t SPR_FLAG[8] = {
    0b00011000, 0b00111000, 0b01011000, 0b00011000,
    0b00011000, 0b00011000, 0b00011000, 0b01111110
};

// 以 scale 倍放大绘制 8x8 精灵
inline void draw_sprite8_scaled(Surface& s, int x, int y,
                                const uint8_t rows[8], uint32_t fg, int scale) {
    for (int r = 0; r < 8; r++) {
        uint8_t bits = rows[r];
        for (int c = 0; c < 8; c++) {
            if (bits & (0x80 >> c)) {
                gfx::fillrect(s, x + c * scale, y + r * scale, scale, scale, fg);
            }
        }
    }
}

// 球拖尾：记录最近 N 个位置，绘制渐隐小圆
struct Trail {
    static const int MAX = 12;
    float xs[MAX], ys[MAX];
    int len;
    void reset() { len = 0; }
    void push(float x, float y) {
        for (int i = MAX - 1; i > 0; i--) { xs[i] = xs[i-1]; ys[i] = ys[i-1]; }
        xs[0] = x; ys[0] = y;
        if (len < MAX) len++;
    }
    void draw(Surface& s, uint32_t color) {
        for (int i = 0; i < len; i++) {
            int r = 3 - i / 4;
            if (r < 1) r = 1;
            gfx::fillcircle(s, (int)xs[i], (int)ys[i], r, color);
        }
    }
};

} // namespace games2
} // namespace nefu
