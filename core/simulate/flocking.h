// nefuOS 仿真引擎库 —— 群体行为 (Flocking / Boids)
// 包含：
//   - Boids 三规则：分离(separation) / 对齐(alignment) / 聚合(cohesion)
//   - 捕食者-猎物 (predator-prey)
//   - 环境障碍规避
//   - 路径跟随
//   - 群体统计 (平均速度/质心/离散度)
#pragma once
#include "particle.h"

namespace nefu {
namespace simulate {

// ============================================================
// 单条 Boid
// ============================================================
struct Boid {
    Vec2 pos;
    Vec2 vel;
    double max_speed;
    double max_force;
    double perception;     // 邻居感知半径
    bool   is_predator;

    Boid();
};

// ============================================================
// 障碍物 (圆形)
// ============================================================
struct Obstacle {
    Vec2 pos;
    double r;
};

// ============================================================
// 群体系统
// ============================================================
struct Flocking {
    Boid*  boids;
    int    cap;
    int    count;
    Obstacle* obstacles;
    int    obs_cap;
    int    obs_count;
    double world_w, world_h;
    double w_sep;        // 分离权重
    double w_ali;        // 对齐权重
    double w_coh;        // 聚合权重
    double w_avoid;      // 障碍规避权重
    double w_flee;       // 逃避捕食者权重
    Vec2   path_target;  // 路径跟随目标
    double w_path;

    void init(int max_boids, int max_obstacles, double w, double h);
    void shutdown();
    void clear();
    void add_boid(const Boid& b);
    void add_obstacle(const Vec2& p, double r);
    void set_path_target(const Vec2& t) { path_target = t; }
    void step(double dt);

    // 统计
    Vec2 centroid() const;
    double avg_speed() const;
    double dispersion() const;   // 与质心的平均距离
    int  predator_count() const;
    int  prey_count() const;

private:
    Vec2 separate(int i) const;
    Vec2 align(int i) const;
    Vec2 cohesion(int i) const;
    Vec2 avoid_obstacles(int i) const;
    Vec2 flee_predators(int i) const;
    Vec2 seek_path(int i) const;
    Vec2 limit_force(const Vec2& f, double maxf) const;
};

// ============================================================
// 自检
// ============================================================
int flocking_self_test();

} // namespace simulate
} // namespace nefu
