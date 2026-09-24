// nefuOS simlib —— 交通流实现 + 自测
#include "simlib/traffic.h"
#include <cstdio>

namespace nefu {
namespace simx {

TrafficFlow::TrafficFlow(int cells_, int vmax_) : cells(cells_), vmax(vmax_), last_avg(0) {
    v.assign(cells, -1);
}

void TrafficFlow::add_car(int pos) {
    pos = (pos % cells + cells) % cells;
    if (v[pos] == -1) v[pos] = 0;
}

void TrafficFlow::clear() {
    for (int i = 0; i < cells; i++) v[i] = -1;
    last_avg = 0;
}

double TrafficFlow::step() {
    // 先并行计算新速度，再移动
    std::vector<int> nv(cells, -1);
    for (int i = 0; i < cells; i++) {
        if (v[i] < 0) continue;
        int speed = v[i];
        // 1) 加速：速度 +1（不超过限速）
        if (speed < vmax) speed++;
        // 2) 计算与前车间距
        int gap = 0;
        for (int k = 1; k <= cells; k++) {
            if (v[(i + k) % cells] >= 0) { gap = k; break; }
        }
        // 3) 制动：不能超过间距
        if (speed > gap) speed = gap;
        // 4) 随机减速：1/4 概率减 1（模拟驾驶员随机行为）
        if (speed > 0 && (i * 2654435761u % 100) < 25) speed--;
        nv[i] = speed;
    }
    // 移动
    std::vector<int> moved(cells, -1);
    for (int i = 0; i < cells; i++) {
        if (nv[i] < 0) continue;
        int np = (i + nv[i]) % cells;
        moved[np] = nv[i];   // 简单模型：碰撞时后者覆盖（近似）
    }
    v = moved;
    // 平均速度
    double sum = 0; int cnt = 0;
    for (int i = 0; i < cells; i++) if (v[i] >= 0) { sum += v[i]; cnt++; }
    last_avg = cnt > 0 ? sum / cnt : 0;
    return last_avg;
}

int TrafficFlow::car_count() const {
    int c = 0;
    for (int i = 0; i < cells; i++) if (v[i] >= 0) c++;
    return c;
}

double TrafficFlow::jam_ratio() const {
    int jam = 0, c = 0;
    for (int i = 0; i < cells; i++) if (v[i] >= 0) { c++; if (v[i] == 0) jam++; }
    return c > 0 ? (double)jam / c : 0;
}

std::string TrafficFlow::to_text() const {
    std::string s;
    for (int i = 0; i < cells; i++) {
        if (v[i] < 0) s.push_back('.');
        else if (v[i] < 10) s.push_back('0' + v[i]);
        else s.push_back('9');
    }
    return s;
}

// ---- self test ----
int TrafficFlow::self_test() {
    int fails = 0;
    // 1. 单辆车自由行驶：速度逐渐升到限速
    {
        TrafficFlow t(100, 5);
        t.add_car(10);
        for (int i = 0; i < 12; i++) t.step();
        if (t.car_count() != 1) fails++;
        double av = t.avg_speed();
        if (av < 4.0 || av > 5.0) fails++;   // 限速 5，无前车，应接近限速
    }
    // 2. 两车间距足够：均能提速
    {
        TrafficFlow t(100, 3);
        t.add_car(10);
        t.add_car(60);
        for (int i = 0; i < 10; i++) t.step();
        if (t.car_count() != 2) fails++;
    }
    // 3. 密集车流：拥堵率较高（速度慢）
    {
        TrafficFlow t(20, 3);
        for (int i = 0; i < 20; i += 2) t.add_car(i);   // 一半密度
        for (int i = 0; i < 30; i++) t.step();
        double jr = t.jam_ratio();
        if (jr < 0.1) fails++;   // 密集应出现部分停车
    }
    // 4. 文本序列化
    {
        TrafficFlow t(10, 9);
        t.add_car(3);
        std::string s = t.to_text();
        if ((int)s.size() != 10) fails++;
        if (s.find('.') == std::string::npos) fails++;
    }
    // 5. 位置取模
    {
        TrafficFlow t(30, 4);
        t.add_car(35);   // 取模到 5
        if (t.car_count() != 1) fails++;
    }
    return fails;
}

} // namespace simx
} // namespace nefu
