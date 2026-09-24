// nefuOS simlib —— 鸟群模拟 boids
// 教学版：Craig Reynolds 经典鸟群（Boids）三规则：
//   1) 分离 Separation：与邻居保持距离
//   2) 对齐 Alignment：与邻居方向一致
//   3) 聚合 Cohesion：向邻居中心靠拢
// 二维平面，无边界环绕。class / STL / cmath / 中文注释。
#pragma once
#include <vector>

namespace nefu {
namespace simx {

// 一只鸟
struct Boid {
    double x, y;     // 位置
    double vx, vy;   // 速度
    Boid() : x(0), y(0), vx(0), vy(0) {}
    Boid(double X, double Y, double VX, double VY) : x(X), y(Y), vx(VX), vy(VY) {}
};

// 鸟群模拟器
class Boids {
public:
    // 构造：width x height 模拟区域，默认参数
    Boids(double width, double height);

    // 添加一只鸟
    void add(double x, double y, double vx, double vy);
    // 清除全部鸟
    void clear();

    // 推进一个时间步：三规则加权调整速度，再更新位置
    void step();

    int size() const { return (int)birds.size(); }
    // 获取第 i 只鸟（只读快照）
    Boid bird(int i) const { return birds[i]; }

    // 权重设置（默认 1.0 / 1.0 / 1.0）
    void set_weights(double sep, double ali, double coh);
    // 邻居感知半径
    void set_radius(double r);

    // 全部鸟的平均速度（衡量"群聚一致性"）
    double avg_speed() const;

    // ---- self test ----
    static int self_test();

private:
    double w, h;
    double r_sep, r_ali, r_coh;   // 分离/对齐/聚合权重
    double radius;                // 邻居半径
    double max_speed;             // 速度上限
    std::vector<Boid> birds;
};

} // namespace simx
} // namespace nefu
