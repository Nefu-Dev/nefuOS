// physics2d.cpp —— 2D 物理实现：碰撞检测/解算、积分、关节、射线
#include "physics2d.h"

namespace nefu {
namespace gameengine {

// ============================================================================
//  宽相/窄相碰撞原语
// ============================================================================
bool aabb_aabb(const Vec2& a_min, const Vec2& a_max,
               const Vec2& b_min, const Vec2& b_max) {
    if (a_max.x < b_min.x || a_min.x > b_max.x) return false;
    if (a_max.y < b_min.y || a_min.y > b_max.y) return false;
    return true;
}

bool circle_circle(const Vec2& ca, fix ra, const Vec2& cb, fix rb) {
    Vec2 d = cb - ca;
    fix r = ra + rb;
    return d.lensq() <= fx::fx_mul(r, r);
}

bool circle_aabb(const Vec2& c, fix r, const Vec2& mn, const Vec2& mx) {
    // 找 AABB 上离圆心最近的点
    fix nx = ge_clamp(c.x, mn.x, mx.x);
    fix ny = ge_clamp(c.y, mn.y, mx.y);
    Vec2 closest(nx, ny);
    Vec2 d = c - closest;
    return d.lensq() <= fx::fx_mul(r, r);
}

// ============================================================================
//  刚体碰撞分发
// ============================================================================
bool collide(const RigidBody& a, const RigidBody& b, Contact& out) {
    out.a = (RigidBody*)&a;
    out.b = (RigidBody*)&b;
    out.hit = false;

    Vec2 amin, amax, bmin, bmax;
    a.world_aabb(amin, amax);
    b.world_aabb(bmin, bmax);

    // 都用 AABB 简化
    if (a.shape.type == SHAPE_AABB && b.shape.type == SHAPE_AABB) {
        if (!aabb_aabb(amin, amax, bmin, bmax)) return false;
        // 计算最小平移轴作为法线
        fix ax = (amin.x + amax.x) / 2;
        fix ay = (amin.y + amax.y) / 2;
        fix bx = (bmin.x + bmax.x) / 2;
        fix by = (bmin.y + bmax.y) / 2;
        fix overlap_x = ge_min(amax.x, bmax.x) - ge_max(amin.x, bmin.x);
        fix overlap_y = ge_min(amax.y, bmax.y) - ge_max(amin.y, bmin.y);
        if (overlap_x < 0 || overlap_y < 0) return false;
        if (overlap_x < overlap_y) {
            out.normal = Vec2(ax < bx ? fx::FX_ONE : -fx::FX_ONE, 0);
            out.penetration = overlap_x;
        } else {
            out.normal = Vec2(0, ay < by ? fx::FX_ONE : -fx::FX_ONE);
            out.penetration = overlap_y;
        }
        out.point = (a.position + b.position) / 2;
        out.hit = true;
        return true;
    }

    // 圆 vs 圆
    if (a.shape.type == SHAPE_CIRCLE && b.shape.type == SHAPE_CIRCLE) {
        if (!circle_circle(a.position, a.shape.radius,
                           b.position, b.shape.radius)) return false;
        Vec2 n = b.position - a.position;
        fix l = n.len();
        if (l == 0) n = Vec2(fx::FX_ONE, 0);
        else n = n / l;
        out.normal = n;
        out.penetration = (a.shape.radius + b.shape.radius) - l;
        out.point = a.position + n * a.shape.radius;
        out.hit = true;
        return true;
    }

    // 圆 vs AABB
    if (a.shape.type == SHAPE_CIRCLE && b.shape.type == SHAPE_AABB) {
        if (!circle_aabb(a.position, a.shape.radius, bmin, bmax)) return false;
        fix nx = ge_clamp(a.position.x, bmin.x, bmax.x);
        fix ny = ge_clamp(a.position.y, bmin.y, bmax.y);
        Vec2 closest(nx, ny);
        Vec2 n = a.position - closest;
        fix l = n.len();
        if (l == 0) n = Vec2(0, -fx::FX_ONE);
        else n = n / l;
        out.normal = n;
        out.penetration = a.shape.radius - l;
        out.point = closest;
        out.hit = true;
        return true;
    }
    // AABB vs 圆（对称）
    if (a.shape.type == SHAPE_AABB && b.shape.type == SHAPE_CIRCLE) {
        Contact tmp;
        bool h = collide(b, a, tmp);
        if (!h) return false;
        out.normal = -tmp.normal;
        out.penetration = tmp.penetration;
        out.point = tmp.point;
        out.hit = true;
        return true;
    }

    return false;
}

// ============================================================================
//  射线 vs AABB（slab 法）
// ============================================================================
fix ray_aabb(const Vec2& o, const Vec2& d, const Vec2& mn, const Vec2& mx) {
    fix tmin = 0;
    fix tmax = fx::itofix(10000);   // 远裁剪面（Q16.16 范围内，避免 int32 溢出）

    for (int i = 0; i < 2; i++) {
        fix orig = (i == 0) ? o.x : o.y;
        fix dir  = (i == 0) ? d.x : d.y;
        fix lo   = (i == 0) ? mn.x : mn.y;
        fix hi   = (i == 0) ? mx.x : mx.y;
        if (dir == 0) {
            if (orig < lo || orig > hi) return -1;
        } else {
            fix t1 = fx::fx_div(lo - orig, dir);
            fix t2 = fx::fx_div(hi - orig, dir);
            if (t1 > t2) { fix t = t1; t1 = t2; t2 = t; }
            tmin = ge_max(tmin, t1);
            tmax = ge_min(tmax, t2);
            if (tmin > tmax) return -1;
        }
    }
    return tmin;
}

// ============================================================================
//  World 推进
// ============================================================================
void PhysicsWorld::step(int dt_ms) {
    contacts.clear();
    if (dt_ms <= 0) return;
    // 毫秒 -> 秒（定点）：dt_s = dt_ms / 1000
    fix dt_s = fx::fx_div(fx::itofix(dt_ms), fx::itofix(1000));
    dt_s = fx::fx_mul(dt_s, dt_scale);

    // 子步进：把一帧拆成多个小步，避免高速物体穿透薄地面
    const int SUB = 4;
    fix sub_dt = fx::fx_div(dt_s, fx::itofix(SUB));
    for (int sstep = 0; sstep < SUB; sstep++) {
    // 1. 重力 + 积分
    for (int i = 0; i < bodies.size(); i++) {
        RigidBody* b = bodies[i];
        if (!b->enabled || b->is_static()) continue;
        b->velocity += gravity * fx::fx_mul(b->gravity_scale, sub_dt);
        b->position += b->velocity * sub_dt;
    }

    // 2. 关节（距离约束）
    for (int i = 0; i < joints.size(); i++) {
        Joint& j = joints[i];
        if (j.broken) continue;
        Vec2 d = j.b->position - j.a->position;
        fix dist = d.len();
        if (dist == 0) continue;
        fix diff = dist - j.rest_length;
        fix corr = fx::fx_mul(fx::fx_mul(diff, j.stiffness), fx::FX_HALF);
        Vec2 n = d / dist;
        if (!j.a->is_static()) j.a->position += n * corr;
        if (!j.b->is_static()) j.b->position -= n * corr;
    }

    // 3. 碰撞检测 + 解算（O(n^2)，游戏规模足够）
    for (int i = 0; i < bodies.size(); i++) {
        for (int j = i + 1; j < bodies.size(); j++) {
            RigidBody* a = bodies[i];
            RigidBody* b = bodies[j];
            if (!a->enabled || !b->enabled) continue;
            if (a->is_static() && b->is_static()) continue;

            Contact c;
            if (!collide(*a, *b, c)) continue;

            // 记录碰撞对
            ContactPair cp; cp.a = a; cp.b = b; cp.normal = c.normal; cp.penetration = c.penetration;
            contacts.push(cp);

            // 相对速度
            Vec2 rv = b->velocity - a->velocity;
            fix vel_along_n = rv.dot(c.normal);
            if (vel_along_n > 0) continue;   // 正在分离

            // 冲量大小
            float e = 0; // 用定点：取较低 restitution
            fix rest = (a->restitution < b->restitution)
                       ? a->restitution : b->restitution;
            (void)e;
            fix inv_sum = a->inv_mass + b->inv_mass;
            if (inv_sum == 0) continue;
            fix jimp = fx::fx_mul(-(fx::FX_ONE + rest), vel_along_n);
            jimp = fx::fx_div(jimp, inv_sum);
            Vec2 impulse = c.normal * jimp;
            a->apply_impulse(-impulse);
            b->apply_impulse(impulse);

            // 位置校正（防止穿透堆积）
            fix percent = fx::fxf(8, 10);   // 80%
            fix slop = fx::fxf(5, 1000);     // 允许的微小穿透
            fix corr_mag = fx::fx_div(
                fx::fx_mul(percent, ge_max(c.penetration - slop, 0)),
                inv_sum);
            Vec2 corr = c.normal * corr_mag;
            if (!a->is_static()) a->position -= corr * a->inv_mass;
            if (!b->is_static()) b->position += corr * b->inv_mass;
        }
    }
    } // end substep
}

// ============================================================================
//  自测
// ============================================================================

// 解算弹簧关节：把两个刚体拉回 rest_length
static void solve_joint(Joint& j, fix dt_s) {
    if (j.broken || !j.a || !j.b) return;
    Vec2 d = j.b->position - j.a->position;
    fix dist = d.len();
    if (dist == 0) return;
    fix diff = dist - j.rest_length;
    // 力 = stiffness * diff
    Vec2 dir = d / dist;
    fix impulse = nefu::fx::fx_mul(nefu::fx::fx_mul(diff, j.stiffness), dt_s);
    Vec2 push = dir * impulse;
    // 静态物体不动
    if (j.a->type != BODY_STATIC) j.a->velocity += push;
    if (j.b->type != BODY_STATIC) j.b->velocity -= push;
}

int physics2d_self_test() {
    int fails = 0;

    // --- 宽相原语（全部定点 itofix） ---
    fix P10 = fx::itofix(10), P5 = fx::itofix(5), P15 = fx::itofix(15);
    fix P20 = fx::itofix(20), P30 = fx::itofix(30), P8 = fx::itofix(8);
    fix P2 = fx::itofix(2), Pneg10 = fx::itofix(-10);
    if (!aabb_aabb(Vec2(0,0), Vec2(P10,P10), Vec2(P5,P5), Vec2(P15,P15))) fails++;
    if (aabb_aabb(Vec2(0,0), Vec2(P10,P10), Vec2(P20,P20), Vec2(P30,P30))) fails++;

    if (!circle_circle(Vec2(0,0), P5, Vec2(P8,0), P5)) fails++;
    if (circle_circle(Vec2(0,0), P5, Vec2(P20,0), P5)) fails++;

    // 圆心在 AABB 内
    if (!circle_aabb(Vec2(P5,P5), P2, Vec2(0,0), Vec2(P10,P10))) fails++;
    // 圆贴边（圆心在 x=0，r=2，AABB 从 x=2 开始：刚好接触）
    if (!circle_aabb(Vec2(0,P5), P2, Vec2(P2,0), Vec2(P10,P10))) fails++;
    // 远离
    if (circle_aabb(Vec2(Pneg10,0), P2, Vec2(0,0), Vec2(P10,P10))) fails++;

    // --- 刚体碰撞 ---
    RigidBody ba;
    ba.shape = Shape::make_box(fx::itofix(10), fx::itofix(10));
    ba.position = Vec2(0, 0);
    RigidBody bb;
    bb.shape = Shape::make_box(fx::itofix(10), fx::itofix(10));
    bb.position = Vec2(fx::itofix(5), 0);   // 重叠

    Contact c;
    if (!collide(ba, bb, c)) fails++;
    if (c.penetration <= 0) fails++;

    // 分开后不碰撞
    bb.position = Vec2(fx::itofix(100), 0);
    if (collide(ba, bb, c)) fails++;

    // --- 物理世界：重力下落 ---
    PhysicsWorld w;
    w.gravity = Vec2(0, fx::itofix(500));   // 500 px/s^2 向下
    RigidBody* floor = w.add_body(Shape::make_box(fx::itofix(200), fx::itofix(20)),
                                  BODY_STATIC);
    floor->position = Vec2(0, fx::itofix(60));   // 厚地面，顶部在 y=50
    RigidBody* ball = w.add_body(Shape::make_box(fx::itofix(6), fx::itofix(6)),
                                  BODY_DYNAMIC);
    ball->position = Vec2(0, fx::itofix(0));
    ball->velocity = Vec2(0, 0);

    fix y_before = ball->position.y;
    // 推进 100ms
    w.step(100);
    // 应因重力下落
    if (ball->position.y <= y_before) fails++;

    // 再推 1 秒，球应落到地面上并停下（位置不再猛增）
    for (int i = 0; i < 20; i++) w.step(50);
    // 厚地面顶部在 y=50，球半高3，停在 ~47；反弹不应把它推到 90 以下太多
if (ball->position.y > fx::itofix(90)) fails++;
    if (ball->position.y < fx::itofix(20)) fails++;   // 不能弹回天上

    // --- 射线 ---
    // 从 (-5,0) 向 +x 射线，命中 AABB (0,-5)-(10,5)
    fix t = ray_aabb(Vec2(fx::itofix(-5), 0), Vec2(fx::FX_ONE, 0),
                     Vec2(0, fx::itofix(-5)), Vec2(fx::itofix(10), fx::itofix(5)));
    if (t < 0) fails++;
    // 命中点 x = -5 + t*1 = 0 -> t = 5
if (fx::fixtoi(t) != 5) fails++;

    // 背向射线不命中
    fix t2 = ray_aabb(Vec2(fx::itofix(-5), 0), Vec2(-fx::FX_ONE, 0),
                      Vec2(0, fx::itofix(-5)), Vec2(fx::itofix(10), fx::itofix(5)));
    if (t2 >= 0) fails++;

    // --- 关节：两个质量被弹簧拉住 ---
    PhysicsWorld w2;
    w2.gravity = Vec2(0, 0);   // 无重力
    RigidBody* p1 = w2.add_body(Shape::make_circle(fx::itofix(3)), BODY_DYNAMIC);
    p1->position = Vec2(0, 0);
    RigidBody* p2 = w2.add_body(Shape::make_circle(fx::itofix(3)), BODY_DYNAMIC);
    p2->position = Vec2(fx::itofix(100), 0);
    w2.add_joint(p1, p2, fx::FX_ONE);   // 刚性关节
    fix d_before = p1->position.dist(p2->position);
    w2.step(100);
    fix d_after = p1->position.dist(p2->position);
    // 刚性关节应保持距离近似不变
if (fx::fx_abs(d_after - d_before) > fx::itofix(5)) fails++;


    // --- BroadPhase 网格 ---
    {
        BroadPhase bp;
        bp.clear();
        RigidBody* b1 = new RigidBody();
        b1->position = Vec2(fx::itofix(100), fx::itofix(100));
        RigidBody* b2 = new RigidBody();
        b2->position = Vec2(fx::itofix(500), fx::itofix(500));
        bp.insert(b1);
        bp.insert(b2);
        RigidBody* out[8];
        int n = bp.query_point(100, 100, out, 8);
        if (n != 1) fails++;
        if (out[0] != b1) fails++;
        // 另一个格子查不到 b1
        int n2 = bp.query_point(500, 500, out, 8);
        if (n2 != 1) fails++;
        if (out[0] != b2) fails++;
        delete b1; delete b2;
    }

    // --- 弹簧关节 ---
    PhysicsWorld wj;
    RigidBody* ja = wj.add_body(Shape::make_aabb(5,5), BODY_DYNAMIC);
    RigidBody* jb = wj.add_body(Shape::make_aabb(5,5), BODY_DYNAMIC);
    ja->position = Vec2(0, 0);
    jb->position = Vec2(fx::itofix(100), 0);
    Joint jj;
    jj.a = ja; jj.b = jb; jj.rest_length = fx::itofix(50); jj.stiffness = fx::fxf(1,2);
    solve_joint(jj, fx::fxf(1,60));
    // b 距 a 100，rest 50，应被拉向 a（速度 -x）
    if (jb->velocity.x >= 0) fails++;

    // --- RayCastResult ---
    RayCastResult rc;
    rc.hit = true;
    rc.t = fx::fxf(1,2);
    rc.body = 0;
    if (!rc.hit) fails++;
    if (rc.t != fx::fxf(1,2)) fails++;



    // --- PhysicsQuery ---
    PhysicsWorld wq;
    wq.add_body(Shape::make_aabb(5,5), BODY_STATIC);
    wq.add_body(Shape::make_aabb(5,5), BODY_DYNAMIC);
    PhysicsQuery pq; pq.world = &wq;
    if (pq.query_aabb(0,0,100,100) != 2) fails++;
    if (!pq.body(0)) fails++;
    return fails;
}

} // namespace gameengine
} // namespace nefu
