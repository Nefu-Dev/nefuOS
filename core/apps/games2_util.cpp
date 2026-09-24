// games2_util.cpp —— 公共工具库的纯逻辑实现（不依赖 gfxlib，便于单测链接）
#include "games2_util.h"

namespace nefu {
namespace games2 {

// ============================================================================
//  Rng: xorshift32
//  参考 Marsaglia xorshift，移位常数 (13, 17, 5) 是经过验证的优良组合
// ============================================================================
uint32_t Rng::next() {
    uint32_t x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return x;
}

int Rng::range(int lo, int hi) {
    if (hi <= lo) return lo;
    uint32_t span = (uint32_t)(hi - lo + 1);
    return lo + (int)(next() % span);
}

// ============================================================================
//  碰撞检测
// ============================================================================
bool rect_overlap(const Rect& a, const Rect& b) {
    if (a.x + a.w <= b.x) return false;
    if (b.x + b.w <= a.x) return false;
    if (a.y + a.h <= b.y) return false;
    if (b.y + b.h <= a.y) return false;
    return true;
}

bool rect_overlap_xy(int ax, int ay, int aw, int ah,
                     int bx, int by, int bw, int bh) {
    Rect a{ax, ay, aw, ah};
    Rect b{bx, by, bw, bh};
    return rect_overlap(a, b);
}

bool point_in_rect(int px, int py, const Rect& r) {
    return px >= r.x && px < r.x + r.w &&
           py >= r.y && py < r.y + r.h;
}

bool circle_rect_collide(int cx, int cy, int cr, const Rect& r) {
    // 找到矩形上离圆心最近的点
    int nx = cx;
    int ny = cy;
    if (cx < r.x)        nx = r.x;
    else if (cx > r.x + r.w - 1) nx = r.x + r.w - 1;
    if (cy < r.y)        ny = r.y;
    else if (cy > r.y + r.h - 1) ny = r.y + r.h - 1;
    int dx = cx - nx;
    int dy = cy - ny;
    return (dx * dx + dy * dy) <= cr * cr;
}

bool circle_circle_collide(int ax, int ay, int ar,
                           int bx, int by, int br) {
    int dx = ax - bx;
    int dy = ay - by;
    int rr = ar + br;
    return (dx * dx + dy * dy) <= rr * rr;
}

// ============================================================================
//  HighTable
// ============================================================================
HighTable::HighTable() {
    // 手动清零，避免依赖 memset（MinGW -O2 下 memset 可能被误优化）
    for (int i = 0; i < HIGH_SCORE_SLOTS; i++) {
        scores[i] = 0;
        names[i][0] = 0;
    }
    count = 0;
}

int HighTable::submit(const char* name, int score) {
    if (score <= 0) return -1;

    // 找到插入位置：从后往前找第一个比 score 小的槽位
    int pos = count;
    while (pos > 0 && scores[pos - 1] < score) {
        pos--;
    }
    if (pos >= HIGH_SCORE_SLOTS) return -1;  // 挤不进 top-N

    // 后面的槽位整体后移一格
    int last = (count < HIGH_SCORE_SLOTS - 1) ? count : HIGH_SCORE_SLOTS - 1;
    while (last > pos) {
        scores[last] = scores[last - 1];
        // 复制名字（手动循环，strncpy 在内核态可用但这里保持一致风格）
        for (int i = 0; i < 16; i++) names[last][i] = names[last - 1][i];
        last--;
    }

    scores[pos] = score;
    for (int i = 0; i < 15; i++) {
        names[pos][i] = name ? name[i] : 0;
        if (!name || !name[i]) break;
    }
    names[pos][15] = 0;

    if (count < HIGH_SCORE_SLOTS) count++;
    return pos;
}

void HighTable::reset() {
    for (int i = 0; i < HIGH_SCORE_SLOTS; i++) {
        scores[i] = 0;
        names[i][0] = 0;
    }
    count = 0;
}

// 全局共享高分表
HighTable g_high;

} // namespace games2
} // namespace nefu
