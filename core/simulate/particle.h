// nefuOS 仿真引擎库 —— 粒子系统 (Particle System)
// 包含：
//   - 粒子结构 (位置/速度/加速度/生命/颜色/大小)
//   - 发射器 (点/线/圆/锥形)
//   - 力场 (重力/风/涡旋/排斥/吸引)
//   - 碰撞 (粒子-平面, 粒子-粒子简化)
//   - 空间哈希 (spatial hash) 加速邻居查询
//   - 粒子池 (无空闲链表预分配池)
// 使用 double 标量运算，无 STL。
#pragma once
#include <stdint.h>

namespace nefu {
namespace simulate {

// ============================================================
// 二维向量
// ============================================================
struct Vec2 {
    double x, y;
    Vec2() : x(0), y(0) {}
    Vec2(double a, double b) : x(a), y(b) {}
    Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
    Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
    Vec2 operator*(double s) const { return Vec2(x * s, y * s); }
    Vec2 operator/(double s) const { return Vec2(x / s, y / s); }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(double s) { x *= s; y *= s; return *this; }
    Vec2& operator/=(double s) { x /= s; y /= s; return *this; }
    double length() const;
    double length_sq() const { return x * x + y * y; }
    Vec2 normalized() const;
    void normalize();
};

// ============================================================
// 粒子
// ============================================================
struct Particle {
    Vec2   pos;
    Vec2   vel;
    Vec2   acc;
    double life;        // 剩余寿命(秒)，<=0 死亡
    double max_life;
    double size;        // 半径(像素)
    uint32_t color;     // 0xAARRGGBB
    bool   active;

    Particle();
    void reset();
    void apply_force(const Vec2& f);
    void integrate(double dt);        // 半隐式欧拉
    double t() const { return max_life > 0 ? life / max_life : 0; }  // 1->0
};

// ============================================================
// 发射器类型
// ============================================================
enum class EmitterType {
    Point,
    Line,
    Circle,
    Cone
};

struct Emitter {
    EmitterType type;
    Vec2   origin;          // 点中心 / 线起点 / 圆心 / 锥顶点
    Vec2   dir;             // 锥方向
    double cone_angle;      // 半角(弧度)
    double length;          // Line 长度 / Circle 半径
    double rate;           // 每秒发射数
    double accumulator;     // 内部计时
    double speed_min, speed_max;
    double life_min, life_max;
    uint32_t color;

    Emitter();
    void emit(Particle* pool, int pool_cap, int& count, int max_out,
              double dt, uint32_t& rng);
};

// ============================================================
// 力场类型
// ============================================================
enum class ForceType {
    Gravity,        // 恒定加速度向量
    Wind,           // 恒定水平风
    Vortex,         // 绕中心点切向力
    Repel,          // 从中心点径向排斥
    Attract         // 向中心点径向吸引
};

struct ForceField {
    ForceType type;
    Vec2   origin;      // 涡旋/排斥/吸引中心
    Vec2   vector;      // Gravity/Wind 向量
    double strength;    // 强度
    double radius;      // 作用半径 (0=无限)

    Vec2 apply(const Particle& p) const;
};

// ============================================================
// 平面碰撞 (一条 y=const 或 x=const 的不可穿墙)
// ============================================================
struct PlaneCollider {
    enum Axis { X, Y } axis;
    double position;        // 该轴上的位置
    double restitution;      // 恢复系数 0..1
    void resolve(Particle& p) const;
};

// ============================================================
// 空间哈希：桶宽 = cell_size
// ============================================================
struct SpatialHash {
    int    cell_size;
    int    cols, rows;
    int*   head;        // 每个桶的头粒子索引 (-1 = 空)
    int*   next;        // 每个粒子的链表下一索引
    int    cap;

    void init(int cw, int cols_, int rows_, int max_particles);
    void shutdown();
    void clear();
    void insert(int idx, double x, double y);
    // 遍历与 (x,y,range) 相交的桶，回调每个邻居索引
    // 返回找到的邻居数 (上限 max_out)
    int query(double x, double y, double range, int* out, int max_out) const;
};

// ============================================================
// 粒子系统 (宿主)
// ============================================================
struct ParticleSystem {
    Particle* particles;
    int   cap;
    int   count;
    Emitter* emitters;
    int   emit_cap;
    int   emit_count;
    ForceField* fields;
    int   field_cap;
    int   field_count;
    PlaneCollider* planes;
    int   plane_cap;
    int   plane_count;
    SpatialHash hash;
    double bounds_w, bounds_h;
    uint32_t rng;

    void init(int max_particles, int max_emitters, int max_fields, int max_planes,
              double w, double h);
    void shutdown();
    void clear();
    void add_emitter(const Emitter& e);
    void add_field(const ForceField& f);
    void add_plane(const PlaneCollider& p);
    // 直接生成一个粒子(发射器外)
    int  spawn(const Particle& p);
    void update(double dt);
    int  alive_count() const;
};

// ============================================================
// 自检：返回失败数
// ============================================================
int particle_self_test();

} // namespace simulate
} // namespace nefu
