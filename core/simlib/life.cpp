// nefuOS simlib —— 生命游戏实现 + 自测
#include "simlib/life.h"
#include <cstdio>

namespace nefu {
namespace simx {

Life::Life(int w_, int h_) : w(w_), h(h_) {
    g.assign(h, std::vector<int>(w, 0));
    ng.assign(h, std::vector<int>(w, 0));
}

void Life::set(int x, int y, int v) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    g[y][x] = v ? 1 : 0;
}

int Life::get(int x, int y) const {
    if (x < 0 || y < 0 || x >= w || y >= h) return 0;
    return g[y][x];
}

int Life::neighbor_count(int x, int y) const {
    int n = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = (x + dx + w) % w;   // 环绕边界
            int ny = (y + dy + h) % h;
            n += g[ny][nx];
        }
    return n;
}

void Life::step() {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int n = neighbor_count(x, y);
            if (g[y][x]) {
                // 活细胞：2~3 邻居存活
                ng[y][x] = (n == 2 || n == 3) ? 1 : 0;
            } else {
                // 死细胞：恰 3 邻居诞生
                ng[y][x] = (n == 3) ? 1 : 0;
            }
        }
    g.swap(ng);
}

int Life::alive_count() const {
    int c = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) c += g[y][x];
    return c;
}

bool Life::is_empty() const { return alive_count() == 0; }

std::string Life::to_text() const {
    std::string s;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) s.push_back(g[y][x] ? '#' : '.');
        if (y + 1 < h) s.push_back('\n');
    }
    return s;
}

bool Life::from_text(const std::string& s) {
    int yy = 0, xx = 0;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '\n') { yy++; xx = 0; continue; }
        if (xx >= w || yy >= h) continue;      // 忽略多余行
        g[yy][xx] = (c == '#' || c == '*') ? 1 : 0;
        xx++;
    }
    return true;
}

// ---- self test ----
int Life::self_test() {
    int fails = 0;
    // 1. 静物块（block）永远不变
    {
        Life l(6, 6);
        l.set(2, 2, 1); l.set(3, 2, 1);
        l.set(2, 3, 1); l.set(3, 3, 1);
        l.step();
        if (l.alive_count() != 4) fails++;
        l.step();
        if (l.alive_count() != 4) fails++;
        if (l.get(2, 2) != 1 || l.get(3, 3) != 1) fails++;
    }
    // 2. 单个细胞会死（邻居不足）
    {
        Life l(5, 5);
        l.set(2, 2, 1);
        l.step();
        if (!l.is_empty()) fails++;
    }
    // 3. 三个横排 → 竖排（blinker 振荡）
    {
        Life l(7, 7);
        l.set(2, 3, 1); l.set(3, 3, 1); l.set(4, 3, 1);
        l.step();
        // 变为竖排：(3,2)(3,3)(3,4)
        if (l.get(3, 2) != 1 || l.get(3, 3) != 1 || l.get(3, 4) != 1) fails++;
        if (l.alive_count() != 3) fails++;
    }
    // 4. 文本序列化
    {
        Life l(3, 2);
        l.set(0, 0, 1); l.set(2, 1, 1);
        std::string t = l.to_text();
        if (t != "#..\n..#") fails++;
        Life m(3, 2);
        m.from_text("#..\n..#");
        if (m.get(0, 0) != 1 || m.get(2, 1) != 1) fails++;
    }
    // 5. set/get 越界保护
    {
        Life l(4, 4);
        l.set(-1, 0, 1);   // 越界忽略
        l.set(9, 9, 1);
        if (l.alive_count() != 0) fails++;
    }
    return fails;
}

} // namespace simx
} // namespace nefu
