// nefuOS 仿真引擎库 —— 粒子系统实现
#include "particle.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace simulate {

// ============================================================
// Vec2
// ============================================================
double Vec2::length() const { return sqrt(x * x + y * y); }
Vec2 Vec2::normalized() const {
    double l = length();
    if (l < 1e-12) return Vec2(0, 0);
    return Vec2(x / l, y / l);
}
void Vec2::normalize() {
    double l = length();
    if (l > 1e-12) { x /= l; y /= l; }
}

// ============================================================
// Particle
// ============================================================
Particle::Particle() { reset(); }
void Particle::reset() {
    pos = Vec2(0, 0); vel = Vec2(0, 0); acc = Vec2(0, 0);
    life = 0; max_life = 0; size = 1; color = 0xFFFFFFFF;
    active = false;
}
void Particle::apply_force(const Vec2& f) { acc += f; }
void Particle::integrate(double dt) {
    // 半隐式欧拉：先更新速度，再用新速度更新位置
    vel += acc * dt;
    pos += vel * dt;
    acc = Vec2(0, 0);
    life -= dt;
    if (life <= 0) active = false;
}

// ============================================================
// Emitter
// ============================================================
Emitter::Emitter() {
    type = EmitterType::Point;
    origin = Vec2(0, 0);
    dir = Vec2(0, 1);
    cone_angle = 0.5;
    length = 10;
    rate = 10;
    accumulator = 0;
    speed_min = 50; speed_max = 100;
    life_min = 1; life_max = 2;
    color = 0xFFFFFFFF;
}

// 简易 LCG 随机数 (确定性，便于自检)
static inline double frand(uint32_t& rng) {
    rng = rng * 1664525u + 1013904223u;
    return (double)(rng >> 8) / (double)(1u << 24);   // [0,1)
}

void Emitter::emit(Particle* pool, int pool_cap, int& count, int max_out,
                    double dt, uint32_t& rng) {
    accumulator += rate * dt;
    int n = (int)accumulator;
    if (n <= 0) return;
    accumulator -= n;
    for (int k = 0; k < n && count < max_out && count < pool_cap; k++) {
        Particle p;
        // 发射起点
        Vec2 start = origin;
        if (type == EmitterType::Line) {
            double t = frand(rng);
            start += Vec2(length * t, 0);
        } else if (type == EmitterType::Circle) {
            double ang = frand(rng) * 6.283185307179586;
            double r = length * frand(rng);
            start += Vec2(cos(ang) * r, sin(ang) * r);
        }
        p.pos = start;
        // 初速度方向
        Vec2 vdir;
        if (type == EmitterType::Cone) {
            double spread = (frand(rng) * 2.0 - 1.0) * cone_angle;
            double ca = cos(spread), sa = sin(spread);
            double dx = dir.x * ca - dir.y * sa;
            double dy = dir.x * sa + dir.y * ca;
            vdir = Vec2(dx, dy);
        } else {
            double ang = frand(rng) * 6.283185307179586;
            vdir = Vec2(cos(ang), sin(ang));
        }
        double sp = speed_min + frand(rng) * (speed_max - speed_min);
        p.vel = vdir * sp;
        double lf = life_min + frand(rng) * (life_max - life_min);
        p.life = lf; p.max_life = lf;
        p.size = 2.0;
        p.color = color;
        p.active = true;
        pool[count++] = p;
    }
}

// ============================================================
// ForceField
// ============================================================
Vec2 ForceField::apply(const Particle& p) const {
    switch (type) {
    case ForceType::Gravity:
    case ForceType::Wind:
        return vector;
    case ForceType::Vortex: {
        Vec2 d = p.pos - origin;
        double r2 = d.length_sq();
        if (r2 < 1.0) r2 = 1.0;
        // 切向: (-dy, dx)
        double s = strength / r2;
        return Vec2(-d.y, d.x) * s;
    }
    case ForceType::Repel:
    case ForceType::Attract: {
        Vec2 d = origin - p.pos;
        double r2 = d.length_sq();
        if (r2 < 1.0) r2 = 1.0;
        if (radius > 0 && sqrt(r2) > radius) return Vec2(0, 0);
        double s = strength / r2;
        if (type == ForceType::Repel) s = -s;
        return d.normalized() * s;
    }
    }
    return Vec2(0, 0);
}

// ============================================================
// PlaneCollider
// ============================================================
void PlaneCollider::resolve(Particle& p) const {
    if (axis == Axis::Y) {
        if (p.pos.y > position && p.vel.y > 0) {
            p.pos.y = position;
            p.vel.y = -p.vel.y * restitution;
        }
    } else {
        if (p.pos.x > position && p.vel.x > 0) {
            p.pos.x = position;
            p.vel.x = -p.vel.x * restitution;
        }
    }
}

// ============================================================
// SpatialHash
// ============================================================
void SpatialHash::init(int cw, int cols_, int rows_, int max_particles) {
    cell_size = cw;
    cols = cols_; rows = rows_;
    cap = max_particles;
    head = new int[cols * rows];
    next = new int[max_particles];
    clear();
}
void SpatialHash::shutdown() {
    delete[] head; head = 0;
    delete[] next; next = 0;
}
void SpatialHash::clear() {
    for (int i = 0; i < cols * rows; i++) head[i] = -1;
}
void SpatialHash::insert(int idx, double x, double y) {
    int cx = (int)(x / cell_size);
    int cy = (int)(y / cell_size);
    if (cx < 0) cx = 0; if (cx >= cols) cx = cols - 1;
    if (cy < 0) cy = 0; if (cy >= rows) cy = rows - 1;
    int bucket = cy * cols + cx;
    next[idx] = head[bucket];
    head[bucket] = idx;
}
int SpatialHash::query(double x, double y, double range, int* out, int max_out) const {
    int cnt = 0;
    int cx0 = (int)((x - range) / cell_size);
    int cy0 = (int)((y - range) / cell_size);
    int cx1 = (int)((x + range) / cell_size);
    int cy1 = (int)((y + range) / cell_size);
    if (cx0 < 0) cx0 = 0; if (cx1 >= cols) cx1 = cols - 1;
    if (cy0 < 0) cy0 = 0; if (cy1 >= rows) cy1 = rows - 1;
    for (int cy = cy0; cy <= cy1; cy++)
        for (int cx = cx0; cx <= cx1; cx++) {
            int i = head[cy * cols + cx];
            while (i >= 0 && cnt < max_out) {
                out[cnt++] = i;
                i = next[i];
            }
        }
    return cnt;
}

// ============================================================
// ParticleSystem
// ============================================================
void ParticleSystem::init(int max_particles, int max_emitters, int max_fields,
                         int max_planes, double w, double h) {
    cap = max_particles;
    particles = new Particle[cap];
    for (int i = 0; i < cap; i++) particles[i].reset();
    count = 0;
    emitters = new Emitter[max_emitters];
    emit_cap = max_emitters; emit_count = 0;
    fields = new ForceField[max_fields];
    field_cap = max_fields; field_count = 0;
    planes = new PlaneCollider[max_planes];
    plane_cap = max_planes; plane_count = 0;
    bounds_w = w; bounds_h = h;
    rng = 12345;
    hash.init(16, (int)(w / 16) + 1, (int)(h / 16) + 1, cap);
}
void ParticleSystem::shutdown() {
    delete[] particles; particles = 0;
    delete[] emitters; emitters = 0;
    delete[] fields; fields = 0;
    delete[] planes; planes = 0;
    hash.shutdown();
}
void ParticleSystem::clear() {
    count = 0;
    for (int i = 0; i < cap; i++) particles[i].reset();
    emit_count = 0; field_count = 0; plane_count = 0;
}
void ParticleSystem::add_emitter(const Emitter& e) {
    if (emit_count < emit_cap) emitters[emit_count++] = e;
}
void ParticleSystem::add_field(const ForceField& f) {
    if (field_count < field_cap) fields[field_count++] = f;
}
void ParticleSystem::add_plane(const PlaneCollider& p) {
    if (plane_count < plane_cap) planes[plane_count++] = p;
}
int ParticleSystem::spawn(const Particle& p) {
    if (count >= cap) return -1;
    particles[count] = p;
    return count++;
}
void ParticleSystem::update(double dt) {
    // 1) 发射器生成
    for (int i = 0; i < emit_count; i++)
        emitters[i].emit(particles, cap, count, cap, dt, rng);

    // 2) 力场累加
    for (int i = 0; i < count; i++) {
        Particle& p = particles[i];
        if (!p.active) continue;
        for (int f = 0; f < field_count; f++)
            p.apply_force(fields[f].apply(p));
    }

    // 3) 积分
    for (int i = 0; i < count; i++) {
        Particle& p = particles[i];
        if (!p.active) continue;
        p.integrate(dt);
    }

    // 4) 平面碰撞
    for (int i = 0; i < count; i++) {
        Particle& p = particles[i];
        if (!p.active) continue;
        for (int pl = 0; pl < plane_count; pl++)
            planes[pl].resolve(p);
    }

    // 5) 空间哈希 + 粒子-粒子简化碰撞 (同桶内半径和检查)
    hash.clear();
    for (int i = 0; i < count; i++)
        if (particles[i].active)
            hash.insert(i, particles[i].pos.x, particles[i].pos.y);
    for (int i = 0; i < count; i++) {
        Particle& a = particles[i];
        if (!a.active) continue;
        int nb[64]; int nn = hash.query(a.pos.x, a.pos.y, a.size * 2, nb, 64);
        for (int k = 0; k < nn; k++) {
            int j = nb[k];
            if (j <= i) continue;
            Particle& b = particles[j];
            if (!b.active) continue;
            Vec2 d = b.pos - a.pos;
            double min_d = a.size + b.size;
            double d2 = d.length_sq();
            if (d2 < min_d * min_d && d2 > 1e-9) {
                double dist = sqrt(d2);
                Vec2 n = d / dist;
                // 位置修正：各推一半
                double overlap = (min_d - dist) * 0.5;
                a.pos -= n * overlap;
                b.pos += n * overlap;
                // 简易冲量交换(假设等质量)
                Vec2 rv = b.vel - a.vel;
                double vn = rv.x * n.x + rv.y * n.y;
                if (vn < 0) {
                    double imp = -vn * 0.8;   // 恢复系数 0.8
                    a.vel -= n * imp;
                    b.vel += n * imp;
                }
            }
        }
    }

    // 6) 清理死亡粒子 (swap-remove)
    int w = 0;
    for (int r = 0; r < count; r++) {
        if (particles[r].active) {
            if (w != r) particles[w] = particles[r];
            w++;
        }
    }
    count = w;
}
int ParticleSystem::alive_count() const { return count; }

// ============================================================
// 自检
// ============================================================
int particle_self_test() {
    int fail = 0;

    // --- 1) 自由落体：(0,0) v=0, g=9.8，1 秒后 y ≈ 0.5*g*t^2 = 4.9 ---
    {
        Particle p;
        p.pos = Vec2(0, 0);
        p.vel = Vec2(0, 0);
        p.life = 10; p.max_life = 10; p.active = true;
        double dt = 0.001;
        for (int i = 0; i < 1000; i++) {
            p.acc = Vec2(0, 9.8);
            p.integrate(dt);
        }
        if (p.pos.y < 4.7 || p.pos.y > 5.1) fail++;
    }

    // --- 2) 发射器 ---
    {
        ParticleSystem sys;
        sys.init(1024, 4, 4, 4, 640, 480);
        Emitter e;
        e.type = EmitterType::Point;
        e.origin = Vec2(320, 100);
        e.rate = 200;
        e.speed_min = 50; e.speed_max = 100;
        e.life_min = 1; e.life_max = 2;
        sys.add_emitter(e);
        ForceField g;
        g.type = ForceType::Gravity; g.vector = Vec2(0, 98);
        sys.add_field(g);
        sys.update(0.5);
        if (sys.alive_count() < 50) fail++;
        sys.shutdown();
    }

    // --- 3) 平面碰撞 ---
    {
        ParticleSystem sys;
        sys.init(64, 2, 2, 2, 640, 480);
        Particle p;
        p.pos = Vec2(320, 300);
        p.vel = Vec2(0, 100);
        p.life = 10; p.max_life = 10; p.active = true;
        sys.spawn(p);
        PlaneCollider pl;
        pl.axis = PlaneCollider::Y; pl.position = 400; pl.restitution = 0.8;
        sys.add_plane(pl);
        for (int i = 0; i < 200; i++) sys.update(0.01);
        if (sys.alive_count() == 0) fail++;
        sys.shutdown();
    }

    // --- 4) 空间哈希 ---
    {
        SpatialHash h;
        h.init(10, 20, 20, 100);
        h.insert(0, 15, 15);
        h.insert(1, 25, 15);
        int out[16];
        int n = h.query(15, 15, 5.0, out, 16);
        if (n < 1) fail++;
        h.shutdown();
    }

    // --- 5) 力场 Attract ---
    {
        ForceField f;
        f.type = ForceType::Attract;
        f.origin = Vec2(0, 0);
        f.strength = 1000;
        f.radius = 0;
        Particle p;
        p.pos = Vec2(10, 0);
        Vec2 a = f.apply(p);
        if (a.x >= 0) fail++;
    }

    // --- 6) 粒子池 ---
    {
        ParticleSystem sys;
        sys.init(8, 1, 1, 1, 100, 100);
        for (int i = 0; i < 20; i++) {
            Particle p; p.pos = Vec2(i, i); p.life = 1; p.active = true;
            sys.spawn(p);
        }
        if (sys.alive_count() != 8) fail++;
        sys.shutdown();
    }

    return fail;
}

} // namespace simulate
} // namespace nefu
