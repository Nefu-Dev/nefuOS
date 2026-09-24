// nefuOS simlib —— Langton 蚂蚁实现 + 自测
#include "simlib/langton.h"
#include <cstdio>

namespace nefu {
namespace simx {

LangtonAnt::LangtonAnt(int w_, int h_) : w(w_), h(h_), total_steps(0) {
    grid.assign(h, std::vector<int>(w, 0));
    ax = w / 2;
    ay = h / 2;
    dir = 0;
}

void LangtonAnt::set(int x, int y, int black) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    grid[y][x] = black ? 1 : 0;
}

int LangtonAnt::get(int x, int y) const {
    if (x < 0 || y < 0 || x >= w || y >= h) return 0;
    return grid[y][x];
}

bool LangtonAnt::step() {
    static const int dx[4] = { 0, 1, 0, -1 };
    static const int dy[4] = { -1, 0, 1, 0 };
    // 当前格颜色决定转向
    if (grid[ay][ax] == 0) {
        // 白格：左转
        dir = (dir + 3) % 4;
        grid[ay][ax] = 1;   // 翻黑
    } else {
        // 黑格：右转
        dir = (dir + 1) % 4;
        grid[ay][ax] = 0;   // 翻白
    }
    ax += dx[dir];
    ay += dy[dir];
    total_steps++;
    if (ax < 0 || ay < 0 || ax >= w || ay >= h) return false;
    return true;
}

int LangtonAnt::black_count() const {
    int c = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) c += grid[y][x];
    return c;
}

std::string LangtonAnt::to_text() const {
    std::string s;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (x == ax && y == ay) s.push_back('A');
            else s.push_back(grid[y][x] ? 'B' : '.');
        }
        if (y + 1 < h) s.push_back('\n');
    }
    return s;
}

int LangtonAnt::run(int n) {
    int done = 0;
    for (int i = 0; i < n; i++) {
        if (!step()) break;
        done++;
    }
    return done;
}

// ---- self test ----
int LangtonAnt::self_test() {
    int fails = 0;
    // 1. 第一步：中心白格 -> 左转朝左，格翻黑，位置 (-1, 0) 偏移
    {
        LangtonAnt a(9, 9);
        a.set(4, 4, 0);   // 显式白
        a.step();
        // 白格：左转（原朝上 -> 朝左），翻黑
        if (a.ant_dir() != 3) fails++;       // 朝左
        if (a.get(4, 4) != 1) fails++;       // 翻黑
        if (a.ant_x() != 3 || a.ant_y() != 4) fails++;
        if (a.steps() != 1) fails++;
    }
    // 2. 黑格右转
    {
        LangtonAnt a(9, 9);
        a.set(4, 4, 1);   // 黑
        a.step();
        // 黑格：右转（朝上 -> 朝右），翻白
        if (a.ant_dir() != 1) fails++;
        if (a.get(4, 4) != 0) fails++;
        if (a.ant_x() != 5 || a.ant_y() != 4) fails++;
    }
    // 3. 走 1000 步：黑格数增长且蚂蚁仍在网格内（中心放大量空间）
    {
        LangtonAnt a(101, 101);
        int done = a.run(1000);
        if (done != 1000) fails++;          // 不应出界
        if (a.black_count() <= 0) fails++;  // 必然翻黑若干格
        if (a.steps() != 1000) fails++;
    }
    // 4. 边界终止
    {
        LangtonAnt a(3, 3);
        a.ax = 1; a.ay = 1;
        int done = a.run(100);
        if (done >= 100) fails++;   // 3x3 网格必然提前出界
    }
    // 5. 文本渲染
    {
        LangtonAnt a(5, 5);
        std::string t = a.to_text();
        if (t.find('A') == std::string::npos) fails++;
    }
    return fails;
}

} // namespace simx
} // namespace nefu
