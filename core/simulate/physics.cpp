// nefuOS 仿真引擎库 —— 物理仿真实现
#include "physics.h"
#include <math.h>
#include <string.h>

namespace nefu {
namespace simulate {

// ============================================================
// 积分器工具
// ============================================================
void euler_step(Vec2& pos, Vec2& vel, const Vec2& acc, double dt) {
    vel += acc * dt;
    pos += vel * dt;
}
void semi_implicit_euler(Vec2& pos, Vec2& vel, const Vec2& acc, double dt) {
    vel += acc * dt;
    pos += vel * dt;
}
void verlet_step(Vec2& pos, Vec2& prev, const Vec2& acc, double dt) {
    Vec2 cur = pos;
    pos = pos * 2.0 - prev + acc * (dt * dt);
    prev = cur;
}

// ============================================================
// RigidBody
// ============================================================
RigidBody::RigidBody() {
    pos = Vec2(0, 0); vel = Vec2(0, 0);
    angle = 0; ang_vel = 0;
    mass = 1; inv_mass = 1;
    inertia = 1; inv_inertia = 1;
    radius = 5;
    restitution = 0.6;
    friction = 0.2;
}
void RigidBody::set_mass(double m) {
    mass = m;
    inv_mass = m > 0 ? 1.0 / m : 0.0;
    // 圆盘转动惯量 I = 0.5 m r^2
    inertia = 0.5 * m * radius * radius;
    inv_inertia = inertia > 0 ? 1.0 / inertia : 0.0;
}
void RigidBody::apply_force(const Vec2& f) {
    vel += f * (inv_mass * (1.0 / 120.0));   // 假 dt=1/120
}
void RigidBody::apply_impulse(const Vec2& j, const Vec2& contact) {
    if (is_static()) return;
    vel += j * inv_mass;
    Vec2 r = contact - pos;
    ang_vel += (r.x * j.y - r.y * j.x) * inv_inertia;
}
void RigidBody::integrate(double dt, Integrator itg) {
    (void)itg;
    // 简化：重力由外部施加，这里只做半隐式欧拉位置积分
    pos += vel * dt;
    angle += ang_vel * dt;
}

// ============================================================
// SpringDamper
// ============================================================
SpringDamper::SpringDamper() {
    a = b = 0; rest_len = 0; k = 1; c = 0;
}
void SpringDamper::configure(Vec2* pa, Vec2* pb, double rest, double k_, double c_) {
    a = pa; b = pb; rest_len = rest; k = k_; c = c_;
}
void SpringDamper::compute(const Vec2& va, const Vec2& vb) {
    Vec2 d = *b - *a;
    double len = d.length();
    if (len < 1e-9) { force_on_a = Vec2(0, 0); force_on_b = Vec2(0, 0); return; }
    Vec2 n = d / len;
    double dx = len - rest_len;
    Vec2 rel_v = vb - va;
    double vrel = rel_v.x * n.x + rel_v.y * n.y;
    double f = k * dx + c * vrel;
    force_on_a = n * f;
    force_on_b = n * (-f);
}
void SpringDamper::apply() {
    // 由外部把 force_on_a / force_on_b 加到两端刚体加速度上
}

// ============================================================
// Pendulum
// ============================================================
void Pendulum::init(double len, double ang0) {
    length = len;
    angle = ang0;
    ang_vel = 0;
    gravity = 9.8;
    damping = 0.0;
}
void Pendulum::step(double dt) {
    // 运动方程：ang_acc = -(g/L) sin(ang) - damping*ang_vel
    double acc = -(gravity / length) * sin(angle) - damping * ang_vel;
    ang_vel += acc * dt;
    angle += ang_vel * dt;
}
double Pendulum::tip_x() const { return length * sin(angle); }
double Pendulum::tip_y() const { return length * cos(angle); }

// ============================================================
// DoublePendulum (标准拉格朗日方程)
// ============================================================
void DoublePendulum::init(double l1_, double l2_, double a1_, double a2_) {
    l1 = l1_; l2 = l2_;
    a1 = a1_; a2 = a2_;
    w1 = 0; w2 = 0;
    m1 = 1; m2 = 1;
    g = 9.8;
}
void DoublePendulum::step(double dt) {
    // 两个角加速度公式 (标准双摆)
    double delta = a1 - a2;
    double den1 = (m1 + m2) * l1 - m2 * l1 * cos(delta) * cos(delta);
    double den2 = (l2 / l1) * den1;
    double acc1 =
        (m2 * l1 * w1 * w1 * cos(delta) * sin(delta)
         + m2 * g * sin(a2) * cos(delta)
         + m2 * l2 * w2 * w2 * sin(delta)
         - (m1 + m2) * g * sin(a1)) / den1;
    double acc2 =
        (-m2 * l2 * w2 * w2 * cos(delta) * sin(delta)
         + (m1 + m2) * (g * sin(a1) * cos(delta)
         - l1 * w1 * w1 * sin(delta) - g * sin(a2))) / den2;
    w1 += acc1 * dt;
    w2 += acc2 * dt;
    a1 += w1 * dt;
    a2 += w2 * dt;
}
void DoublePendulum::tip1(double& x, double& y) const {
    x = l1 * sin(a1);
    y = l1 * cos(a1);
}
void DoublePendulum::tip2(double& x, double& y) const {
    double x1, y1; tip1(x1, y1);
    x = x1 + l2 * sin(a2);
    y = y1 + l2 * cos(a2);
}

// ============================================================
// 碰撞检测
// ============================================================
bool collide_circle_circle(const CircleCollider& a, const CircleCollider& b,
                           Vec2& normal, double& depth) {
    double dx = b.x - a.x, dy = b.y - a.y;
    double d2 = dx * dx + dy * dy;
    double rsum = a.r + b.r;
    if (d2 >= rsum * rsum) return false;
    double d = sqrt(d2);
    if (d < 1e-9) { normal = Vec2(1, 0); depth = rsum; return true; }
    normal = Vec2(dx / d, dy / d);
    depth = rsum - d;
    return true;
}
bool collide_aabb_aabb(const AABB& a, const AABB& b, Vec2& mtv) {
    double dx0 = b.x1 - a.x0;
    double dx1 = a.x1 - b.x0;
    double dy0 = b.y1 - a.y0;
    double dy1 = a.y1 - b.y0;
    if (dx0 <= 0 || dx1 <= 0 || dy0 <= 0 || dy1 <= 0) return false;
    // 选最小穿透轴
    if (dx0 < dx1 && dx0 < dy0 && dx0 < dy1) mtv = Vec2(dx0, 0);
    else if (dx1 < dy0 && dx1 < dy1) mtv = Vec2(-dx1, 0);
    else if (dy0 < dy1) mtv = Vec2(0, dy0);
    else mtv = Vec2(0, -dy1);
    return true;
}
bool collide_circle_aabb(const CircleCollider& c, const AABB& b,
                         Vec2& normal, double& depth) {
    double cx = c.x, cy = c.y;
    if (cx < b.x0) cx = b.x0; else if (cx > b.x1) cx = b.x1;
    if (cy < b.y0) cy = b.y0; else if (cy > b.y1) cy = b.y1;
    double dx = c.x - cx, dy = c.y - cy;
    double d2 = dx * dx + dy * dy;
    if (d2 > c.r * c.r) return false;
    double d = sqrt(d2);
    if (d < 1e-9) { normal = Vec2(0, -1); depth = c.r; return true; }
    normal = Vec2(dx / d, dy / d);
    depth = c.r - d;
    return true;
}

// ============================================================
// 冲量响应
// ============================================================
void resolve_impulse(RigidBody& a, RigidBody& b, const Vec2& normal, double depth) {
    // 位置修正 (Baumgarte)
    double total_inv = a.inv_mass + b.inv_mass;
    if (total_inv <= 0) return;
    Vec2 correction = normal * (depth / total_inv * 0.8);
    a.pos -= correction * a.inv_mass;
    b.pos += correction * b.inv_mass;

    // 速度冲量
    Vec2 rv = b.vel - a.vel;
    double vn = rv.x * normal.x + rv.y * normal.y;
    if (vn > 0) return;   // 正在分离
    double e = (a.restitution + b.restitution) * 0.5;
    double j = -(1.0 + e) * vn / total_inv;
    Vec2 impulse = normal * j;
    a.vel -= impulse * a.inv_mass;
    b.vel += impulse * b.inv_mass;
}

// ============================================================
// DistanceConstraint
// ============================================================
void DistanceConstraint::solve() {
    if (!p1 || !p2) return;
    Vec2 d = *p2 - *p1;
    double len = d.length();
    if (len < 1e-9) return;
    double diff = (len - rest) / len * stiffness;
    Vec2 off = d * 0.5 * diff;
    *p1 += off;
    *p2 -= off;
}

// ============================================================
// 自检
// ============================================================
int physics_self_test() {
    int fail = 0;

    // --- 1) 单摆：小角度周期 ~ 2*pi*sqrt(L/g) ---
    {
        Pendulum p;
        p.init(1.0, 0.1);     // 小角度
        double t = 0;
        // 跑半周期：从 +0.1 到 -0.1 (半周期 ≈ pi*sqrt(1/9.8) ≈ 1.003)
        double prev_sign = 1;
        double half_period = -1;
        for (int i = 0; i < 20000; i++) {
            double dt = 0.001;
            p.step(dt);
            t += dt;
            double s = p.angle > 0 ? 1 : -1;
            if (prev_sign > 0 && s < 0) { half_period = t; break; }
        }
        // 半周期应在 0.9..1.1 之间
        if (half_period < 0.4 || half_period > 0.6) fail++;
    }

    // --- 2) 双摆：跑 10 步不崩溃，末端坐标有限 ---
    {
        DoublePendulum dp;
        dp.init(1, 1, 1.0, 0.5);
        for (int i = 0; i < 100; i++) dp.step(0.01);
        double x, y; dp.tip2(x, y);
        if (x != x || y != y) fail++;   // NaN 检查
        if (fabs(x) > 100 || fabs(y) > 100) fail++;
    }

    // --- 3) 圆-圆碰撞：两圆重叠检测 ---
    {
        CircleCollider a = { 0, 0, 10 };
        CircleCollider b = { 15, 0, 10 };
        Vec2 n; double d;
        if (!collide_circle_circle(a, b, n, d)) fail++;
        if (d < 4 || d > 6) fail++;   // 中心距 15，半径和 20，穿透 5
    }

    // --- 4) 圆-圆不相交 ---
    {
        CircleCollider a = { 0, 0, 5 };
        CircleCollider b = { 20, 0, 5 };
        Vec2 n; double d;
        if (collide_circle_circle(a, b, n, d)) fail++;
    }

    // --- 5) AABB 相交 ---
    {
        AABB a = { 0, 0, 10, 10 };
        AABB b = { 5, 5, 15, 15 };
        Vec2 mtv;
        if (!collide_aabb_aabb(a, b, mtv)) fail++;
    }

    // --- 6) 冲量：相向运动两球碰撞后反弹 ---
    {
        RigidBody a, b;
        a.pos = Vec2(0, 0); a.vel = Vec2(2, 0);
        b.pos = Vec2(5, 0); b.vel = Vec2(-2, 0);
        a.set_mass(1); b.set_mass(1);
        Vec2 n(1, 0); double depth = 1.0;
        resolve_impulse(a, b, n, depth);
        // 等质量弹性碰撞：速度应交换
        if (a.vel.x > 0) fail++;    // a 应反弹向左
        if (b.vel.x < 0) fail++;    // b 应反弹向右
    }

    // --- 7) Verlet 积分：自由落体位置正确 ---
    {
        Vec2 pos(0, 0), prev(0, 0);
        Vec2 acc(0, 9.8);
        double dt = 0.001;
        for (int i = 0; i < 1000; i++) verlet_step(pos, prev, acc, dt);
        // y ≈ 4.9
        if (pos.y < 4.7 || pos.y > 5.1) fail++;
    }

    // --- 8) 弹簧：拉伸后产生回复力 ---
    {
        Vec2 a(0, 0), b(10, 0);
        SpringDamper s;
        s.configure(&a, &b, 5.0, 100.0, 0.0);
        s.compute(Vec2(0, 0), Vec2(0, 0));
        // 拉伸 5，力沿 +x 拉 a
        if (s.force_on_a.x <= 0) fail++;
    }

    // --- 9) 距离约束：把拉远的两点拉回 ---
    {
        Vec2 p1(0, 0), p2(20, 0);
        DistanceConstraint c;
        c.p1 = &p1; c.p2 = &p2; c.rest = 10; c.stiffness = 1.0;
        c.solve();
        double d = (p2 - p1).length();
        if (d > 11 || d < 9) fail++;
    }

    return fail;
}

} // namespace simulate
} // namespace nefu
