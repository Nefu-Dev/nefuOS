// nefuOS simlib —— 鸟群模拟实现 + 自测
#include "simlib/boids.h"
#include <cmath>
#include <cstdio>

namespace nefu {
namespace simx {

Boids::Boids(double width, double height)
    : w(width), h(height), r_sep(1.0), r_ali(1.0), r_coh(1.0), radius(30.0), max_speed(4.0) {}

void Boids::add(double x, double y, double vx, double vy) {
    birds.push_back(Boid(x, y, vx, vy));
}

void Boids::clear() { birds.clear(); }

void Boids::set_weights(double sep, double ali, double coh) {
    r_sep = sep; r_ali = ali; r_coh = coh;
}

void Boids::set_radius(double r) { radius = r; }

void Boids::step() {
    std::vector<Boid> next;
    for (size_t i = 0; i < birds.size(); i++) {
        const Boid& b = birds[i];
        // 三规则累加向量
        double sx = 0, sy = 0;   // 分离
        double ax = 0, ay = 0;   // 对齐
        double cx = 0, cy = 0;   // 聚合
        int nb = 0;
        for (size_t j = 0; j < birds.size(); j++) {
            if (j == i) continue;
            const Boid& o = birds[j];
            double dx = o.x - b.x, dy = o.y - b.y;
            double d2 = dx * dx + dy * dy;
            if (d2 > radius * radius) continue;
            nb++;
            if (d2 > 0.0001 && d2 < 400) {   // 20^2：太近则分离
                double d = std::sqrt(d2);
                sx -= (dx / d) * (40.0 / d);   // 越近推力越大
                sy -= (dy / d) * (40.0 / d);
            }
            ax += o.vx; ay += o.vy;
            cx += o.x;  cy += o.y;
        }
        double vx = b.vx, vy = b.vy;
        if (nb > 0) {
            ax /= nb; ay /= nb;
            cx /= nb; cy /= nb;
            vx += r_sep * sx + r_ali * (ax - b.vx) + r_coh * (cx - b.x) * 0.01;
            vy += r_sep * sy + r_ali * (ay - b.vy) + r_coh * (cy - b.y) * 0.01;
        }
        // 限速
        double sp = std::sqrt(vx * vx + vy * vy);
        if (sp > max_speed) { vx *= max_speed / sp; vy *= max_speed / sp; }
        // 边界软约束：靠近边缘向内转向
        if (b.x < 30) vx += 1.0;
        if (b.x > w - 30) vx -= 1.0;
        if (b.y < 30) vy += 1.0;
        if (b.y > h - 30) vy -= 1.0;
        Boid n = b;
        n.vx = vx; n.vy = vy;
        n.x += vx; n.y += vy;
        if (n.x < 0) n.x = 0;
        if (n.x > w) n.x = w;
        if (n.y < 0) n.y = 0;
        if (n.y > h) n.y = h;
        next.push_back(n);
    }
    birds = next;
}

double Boids::avg_speed() const {
    double s = 0;
    for (size_t i = 0; i < birds.size(); i++)
        s += std::sqrt(birds[i].vx * birds[i].vx + birds[i].vy * birds[i].vy);
    if (birds.empty()) return 0;
    return s / (double)birds.size();
}

// ---- self test ----
int Boids::self_test() {
    int fails = 0;
    // 1. 单鸟不动规则：无邻居时速度保持（边界力可能影响，用中心位置）
    {
        Boids b(500, 500);
        b.add(250, 250, 1.0, 0.0);
        b.step();
        Boid n = b.bird(0);
        if (std::abs(n.vx - 1.0) > 0.5) fails++;   // 无邻居、离边界远：速度基本不变
    }
    // 2. 两只相邻鸟应因分离而远离
    {
        Boids b(500, 500);
        b.add(250, 250, 0, 0);
        b.add(260, 250, 0, 0);   // 相距 10 < 20，触发分离
        b.step();
        Boid a = b.bird(0), c = b.bird(1);
        double d1 = std::sqrt((a.x - c.x) * (a.x - c.x) + (a.y - c.y) * (a.y - c.y));
        if (d1 <= 10.0) fails++;   // 应拉开
        b.step();
        Boid a2 = b.bird(0), c2 = b.bird(1);
        double d2 = std::sqrt((a2.x - c2.x) * (a2.x - c2.x) + (a2.y - c2.y) * (a2.y - c2.y));
        if (d2 <= 10.0) fails++;   // 4 步后仍应保持拉开
    }
    // 3. 对齐：同向邻居让个体速度趋同
    {
        Boids b(500, 500);
        b.add(100, 100, 2.0, 1.0);
        b.add(120, 100, 2.0, 1.0);   // 同向邻居，相距 20 在半径内
        b.step();
        Boid n = b.bird(0);
        // 邻居同向：对齐项 (neighbor - self) 小，速度变化不大
        if (std::abs(n.vx - 2.0) > 1.0) fails++;
    }
    // 4. 群平均速度非负且有限
    {
        Boids b(200, 200);
        for (int i = 0; i < 10; i++) b.add(100.0 + i * 5, 100.0, (double)(i % 3), (double)(i % 2));
        for (int s = 0; s < 20; s++) b.step();
        double av = b.avg_speed();
        if (av < 0 || av > 10) fails++;
        if (b.size() != 10) fails++;
    }
    // 5. 边界夹紧
    {
        Boids b(100, 100);
        b.add(5, 5, -2, -2);   // 角落，负速度向外
        for (int s = 0; s < 30; s++) b.step();
        Boid n = b.bird(0);
        if (n.x < 0 || n.y < 0) fails++;
        if (n.x > 100 || n.y > 100) fails++;
    }
    return fails;
}

} // namespace simx
} // namespace nefu
