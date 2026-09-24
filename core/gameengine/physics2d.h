// physics2d.h —— 2D 刚体物理：AABB/圆/多边形碰撞、解算、重力、摩擦、restitution、关节、射线
//
// 纯逻辑，不依赖 gfxlib，可独立自测。全部定点 Q16.16。
#pragma once

#include <stdint.h>
#include "../klib/klib.h"
#include "ge_math.h"

namespace nefu {
namespace gameengine {

// 刚体类型
enum BodyType {
    BODY_STATIC = 0,    // 无限质量，不动（地形/墙）
    BODY_KINEMATIC,     // 手动速度驱动
    BODY_DYNAMIC,       // 受重力/力影响
};

// 形状类型
enum ShapeType {
    SHAPE_AABB = 0,
    SHAPE_CIRCLE,
    SHAPE_POLYGON,
};

const int PHYS_MAX_POLY = 8;   // 多边形最大顶点数

// ============================================================================
//  Shape —— 碰撞形状（AABB / 圆 / 凸多边形）
// ============================================================================
struct Shape {
    ShapeType type;
    // AABB：半宽半高（相对中心）
    fix hw, hh;
    // 圆：半径
    fix radius;
    // 多边形：局部顶点（最多 PHYS_MAX_POLY）
    Vec2 verts[PHYS_MAX_POLY];
    int  vcount;

    Shape() : type(SHAPE_AABB), hw(fx::FX_ONE), hh(fx::FX_ONE),
              radius(fx::FX_ONE), vcount(0) {}

    static Shape make_aabb(fix half_w, fix half_h) {
        Shape s; s.type = SHAPE_AABB; s.hw = half_w; s.hh = half_h; return s;
    }
    static Shape make_circle(fix r) {
        Shape s; s.type = SHAPE_CIRCLE; s.radius = r; return s;
    }
    static Shape make_box(fix w, fix h) {
        return make_aabb(fx::fx_div(w, fx::itofix(2)), fx::fx_div(h, fx::itofix(2)));
    }
};

// ============================================================================
//  RigidBody —— 刚体
// ============================================================================
struct RigidBody {
    Vec2    position;
    Vec2    velocity;
    fix     rotation;       // 弧度（演示用，这里不做旋转碰撞）
    fix     mass;
    fix     inv_mass;       // 1/mass；静态体为 0
    fix     restitution;    // 弹性系数 0..1
    fix     friction;        // 摩擦系数 0..1
    fix     gravity_scale;
    BodyType type;
    Shape   shape;
    bool    enabled;
    int     id;             // 调试用
    void*   userdata;

    RigidBody() : rotation(0), mass(fx::FX_ONE), inv_mass(fx::FX_ONE),
                  restitution(fx::fxf(2,10)), friction(fx::fxf(3,10)),
                  gravity_scale(fx::FX_ONE), type(BODY_DYNAMIC),
                  enabled(true), id(0), userdata(0) {}

    void set_static() {
        type = BODY_STATIC; mass = 0; inv_mass = 0;
    }
    bool is_static() const { return type == BODY_STATIC || inv_mass == 0; }

    void apply_impulse(const Vec2& j) {
        if (is_static()) return;
        velocity += j * inv_mass;
    }

    // 世界 AABB（用于宽相剔除）
    void world_aabb(Vec2& out_min, Vec2& out_max) const {
        if (shape.type == SHAPE_CIRCLE) {
            out_min = position - Vec2(shape.radius, shape.radius);
            out_max = position + Vec2(shape.radius, shape.radius);
        } else {
            out_min = position - Vec2(shape.hw, shape.hh);
            out_max = position + Vec2(shape.hw, shape.hh);
        }
    }
};

// ============================================================================
//  碰撞接触信息
// ============================================================================
struct Contact {
    RigidBody* a;
    RigidBody* b;
    Vec2 normal;        // 从 a 指向 b
    fix  penetration;   // 穿透深度
    Vec2 point;         // 接触点
    bool hit;
};

// ============================================================================
//  Joint —— 距离/弹簧关节
// ============================================================================
struct Joint {
    RigidBody* a;
    RigidBody* b;
    fix  rest_length;
    fix  stiffness;     // 0..1
    bool broken;
};

// 碰撞对记录
struct ContactPair { RigidBody* a; RigidBody* b; Vec2 normal; fix penetration; };
struct ContactListener { virtual ~ContactListener() {} virtual void on_begin_contact(RigidBody*,RigidBody*){} virtual void on_end_contact(RigidBody*,RigidBody*){} };

// ============================================================================
//  World —— 物理世界
// ============================================================================
struct PhysicsWorld {
    List<RigidBody*> bodies;
    List<Joint> joints;
    List<ContactPair> contacts;
    Vec2 gravity;         // 像素/秒^2
    fix  dt_scale;         // 时间缩放
    int  position_iterations;

    PhysicsWorld() : gravity(0, fx::itofix(980)), dt_scale(fx::FX_ONE),
                     position_iterations(8) {}

    ~PhysicsWorld() {
        for (int i = 0; i < bodies.size(); i++) delete bodies[i];
        bodies.clear();
        joints.clear();
    }

    RigidBody* add_body(const Shape& s, BodyType t) {
        RigidBody* b = new RigidBody();
        b->shape = s;
        b->type = t;
        if (t == BODY_STATIC) { b->inv_mass = 0; b->mass = 0; }
        else { b->mass = fx::FX_ONE; b->inv_mass = fx::FX_ONE; }
        bodies.push(b);
        return b;
    }

    void add_joint(RigidBody* a, RigidBody* b, fix stiffness) {
        Joint j;
        j.a = a; j.b = b;
        j.rest_length = a->position.dist(b->position);
        j.stiffness = stiffness;
        j.broken = false;
        joints.push(j);
    }

    // 推进世界；dt_ms 为毫秒
    void step(int dt_ms);

    int body_count() const { return bodies.size(); }
    int contact_count() const { return contacts.size(); }
};

// ============================================================================
//  碰撞检测（纯函数，供自测）
// ============================================================================
// AABB vs AABB（世界中心 + 半宽）
bool aabb_aabb(const Vec2& a_min, const Vec2& a_max,
               const Vec2& b_min, const Vec2& b_max);

// 圆 vs 圆
bool circle_circle(const Vec2& ca, fix ra, const Vec2& cb, fix rb);

// 圆 vs AABB
bool circle_aabb(const Vec2& c, fix r, const Vec2& mn, const Vec2& mx);

// 两个刚体之间的碰撞检测（分发到上述函数）
bool collide(const RigidBody& a, const RigidBody& b, Contact& out);

// 射线 vs AABB：返回 t ∈ [0,1] 命中参数，未命中返回 -1
// o = 原点，d = 方向（单位向量）
fix ray_aabb(const Vec2& o, const Vec2& d, const Vec2& mn, const Vec2& mx);

// ============================================================================
//  自测
// ============================================================================

// ============================================================================
//  BroadPhase —— 均匀网格宽相：把刚体按世界坐标分桶，减少两两检测
// ============================================================================
const int GE_CELL = 64;     // 格子边长（像素）
const int GE_GRID_W = 32, GE_GRID_H = 32;

struct BroadPhase {
    // 每格最多存 8 个体引用
    struct Cell { int count; RigidBody* items[8]; };
    Cell grid[GE_GRID_W][GE_GRID_H];

    void clear() {
        for (int y = 0; y < GE_GRID_H; y++)
            for (int x = 0; x < GE_GRID_W; x++)
                grid[y][x].count = 0;
    }
    void insert(RigidBody* b) {
        int cx = fx::fixtoi(b->position.x) / GE_CELL;
        int cy = fx::fixtoi(b->position.y) / GE_CELL;
        if (cx < 0) cx = 0; if (cx >= GE_GRID_W) cx = GE_GRID_W - 1;
        if (cy < 0) cy = 0; if (cy >= GE_GRID_H) cy = GE_GRID_H - 1;
        Cell& cell = grid[cy][cx];
        if (cell.count < 8) cell.items[cell.count++] = b;
    }
    // 查询某点附近的刚体（写入 out，返回数量）
    int query_point(int px, int py, RigidBody** out, int out_max) {
        int cx = px / GE_CELL, cy = py / GE_CELL;
        if (cx < 0 || cx >= GE_GRID_W || cy < 0 || cy >= GE_GRID_H) return 0;
        int n = 0;
        Cell& cell = grid[cy][cx];
        for (int i = 0; i < cell.count && n < out_max; i++)
            out[n++] = cell.items[i];
        return n;
    }
};

// ============================================================================
//  RaycastResult —— 射线查询结果
// ============================================================================
struct RaycastResult {
    RigidBody* body;
    fix  t;          // 命中距离（参数）
    Vec2 point;
    bool hit;
    RaycastResult() : body(0), t(0), hit(false) {}
};


// ============================================================================
//  RayCastResult —— 射线检测结果
// ============================================================================
struct RayCastResult {
    bool hit;
    fix  t;          // 命中参数 0..1
    Vec2 point;
    RigidBody* body;
    Vec2 normal;
};

// ======================================================================