// nefuOS 仿真引擎库 —— 物理仿真 (Physics)
// 包含：
//   - 刚体 (位置/速度/角速度/质量/转动惯量)
//   - 弹簧-阻尼系统
//   - 单摆 / 双摆
//   - 碰撞检测 (AABB / 圆 / 多边形简化)
//   - 碰撞响应 (冲量)
//   - Verlet / Euler / 半隐式 Euler 积分
//   - 约束求解 (距离约束 / 弹簧约束)
#pragma once
#include "particle.h"   // 复用 Vec2

namespace nefu {
namespace simulate {

// ============================================================
// 积分器类型
// ============================================================
enum class Integrator {
    Euler,          // 显式欧拉 (不稳定)
    SemiImplicit,   // 半隐式欧拉 (symplectic, 推荐)
    Verlet          // 位置 Verlet
};

// ============================================================
// 刚体 (二维圆盘近似)
// ============================================================
struct RigidBody {
    Vec2   pos;
    Vec2   vel;
    double angle;       // 朝向
    double ang_vel;     // 角速度
    double mass;
    double inv_mass;    // 1/mass (0 = 静态)
    double inertia;
    double inv_inertia;
    double radius;      // 圆盘半径
    double restitution;
    double friction;

    RigidBody();
    void set_mass(double m);
    void apply_force(const Vec2& f);
    void apply_impulse(const Vec2& j, const Vec2& contact);
    void integrate(double dt, Integrator itg);
    bool is_static() const { return inv_mass == 0; }
};

// ============================================================
// 弹簧-阻尼系统 (连接两点)
// ============================================================
struct SpringDamper {
    Vec2*  a;           // 端点 A (位置指针)
    Vec2*  b;           // 端点 B
    double rest_len;
    double k;           // 刚度
    double c;           // 阻尼
    Vec2   force_on_a; // 上一步计算结果
    Vec2   force_on_b;

    SpringDamper();
    void configure(Vec2* pa, Vec2* pb, double rest, double k, double c);
    void compute(const Vec2& va, const Vec2& vb);   // 计算力
    void apply();                                   // 把力写到 acc (由调用方加)
};

// ============================================================
// 单摆 (小角度/大角度)
// ============================================================
struct Pendulum {
    double angle;       // 相对竖直向下的角度
    double ang_vel;
    double length;
    double gravity;
    double damping;

    void init(double len, double ang0);
    void step(double dt);       // 半隐式欧拉
    double tip_x() const;      // 摆端坐标 (支点在原点)
    double tip_y() const;
};

// ============================================================
// 双摆 (混沌演示)
// ============================================================
struct DoublePendulum {
    double a1, a2;       // 两段角度
    double w1, w2;       // 角速度
    double l1, l2;       // 长度
    double m1, m2;       // 质量
    double g;

    void init(double l1_, double l2_, double a1_, double a2_);
    void step(double dt);    // RK4 简化积分
    void tip1(double& x, double& y) const;
    void tip2(double& x, double& y) const;
};

// ============================================================
// 碰撞体
// ============================================================
struct CircleCollider { double x, y, r; };
struct AABB { double x0, y0, x1, y1; };

// 圆-圆相交；返回 true 并填充法线(从 a 指向 b)和重叠深度
bool collide_circle_circle(const CircleCollider& a, const CircleCollider& b,
                           Vec2& normal, double& depth);
// AABB-AABB 相交；返回 true 并填充最小平移向量
bool collide_aabb_aabb(const AABB& a, const AABB& b, Vec2& mtv);
// 圆-AABB 相交
bool collide_circle_aabb(const CircleCollider& c, const AABB& b,
                        Vec2& normal, double& depth);

// 冲量求解：给定两刚体接触法线和重叠深度，修正速度与位置
void resolve_impulse(RigidBody& a, RigidBody& b, const Vec2& normal, double depth);

// ============================================================
// 距离约束 (布料/绳索)
// ============================================================
struct DistanceConstraint {
    Vec2* p1;
    Vec2* p2;
    double rest;
    double stiffness;    // 0..1
    void solve();
};

// ============================================================
// 积分器工具
// ============================================================
void euler_step(Vec2& pos, Vec2& vel, const Vec2& acc, double dt);
void semi_implicit_euler(Vec2& pos, Vec2& vel, const Vec2& acc, double dt);
// Verlet：需要上一步位置
void verlet_step(Vec2& pos, Vec2& prev, const Vec2& acc, double dt);

// ============================================================
// 自检
// ============================================================
int physics_self_test();

} // namespace simulate
} // namespace nefu
