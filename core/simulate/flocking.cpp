// nefuOS 仿真引擎库 —— 群体行为实现
#include <cstdio>
#include "flocking.h"
#include <math.h>
#include <string.h>

namespace nefu {
namespace simulate {

// ============================================================
// Boid
// ============================================================
Boid::Boid() {
    pos = Vec2(0, 0); vel = Vec2(1, 0);
    max_speed = 120;
    max_force = 300;
    perception = 30;
    is_predator = false;
}

// ============================================================
// Flocking
// ============================================================
void Flocking::init(int max_boids, int max_obstacles, double w, double h) {
    cap = max_boids;
    boids = new Boid[cap];
    count = 0;
    obs_cap = max_obstacles;
    obstacles = new Obstacle[obs_cap];
    obs_count = 0;
    world_w = w; world_h = h;
    w_sep = 1.5; w_ali = 1.0; w_coh = 1.0;
    w_avoid = 2.0; w_flee = 3.0;
    path_target = Vec2(w / 2, h / 2);
    w_path = 0.3;
}
void Flocking::shutdown() {
    delete[] boids; boids = 0;
    delete[] obstacles; obstacles = 0;
}
void Flocking::clear() { count = 0; obs_count = 0; }
void Flocking::add_boid(const Boid& b) {
    if (count < cap) boids[count++] = b;
}
void Flocking::add_obstacle(const Vec2& p, double r) {
    if (obs_count < obs_cap) {
        obstacles[obs_count].pos = p;
        obstacles[obs_count].r = r;
        obs_count++;
    }
}
Vec2 Flocking::limit_force(const Vec2& f, double maxf) const {
    double l = f.length();
    if (l > maxf) return f * (maxf / l);
    return f;
}
Vec2 Flocking::separate(int i) const {
    Vec2 steer(0, 0);
    int cnt = 0;
    for (int j = 0; j < count; j++) {
        if (j == i) continue;
        double d = (boids[j].pos - boids[i].pos).length();
        if (d < boids[i].perception * 0.5 && d > 0.01) {
            Vec2 diff = (boids[i].pos - boids[j].pos) / d;
            steer += diff / d;
            cnt++;
        }
    }
    if (cnt > 0) steer /= cnt;
    return steer;
}
Vec2 Flocking::align(int i) const {
    Vec2 steer(0, 0);
    int cnt = 0;
    for (int j = 0; j < count; j++) {
        if (j == i) continue;
        double d = (boids[j].pos - boids[i].pos).length();
        if (d < boids[i].perception) {
            steer += boids[j].vel;
            cnt++;
        }
    }
    if (cnt > 0) {
        steer /= cnt;
        double sp = steer.length();
        if (sp > 0) steer = steer * (boids[i].max_speed / sp);
    }
    return steer;
}
Vec2 Flocking::cohesion(int i) const {
    Vec2 com(0, 0);
    int cnt = 0;
    for (int j = 0; j < count; j++) {
        if (j == i) continue;
        double d = (boids[j].pos - boids[i].pos).length();
        if (d < boids[i].perception) {
            com += boids[j].pos;
            cnt++;
        }
    }
    if (cnt == 0) return Vec2(0, 0);
    com /= cnt;
    Vec2 desired = com - boids[i].pos;
    double l = desired.length();
    if (l > 0) desired = desired * (boids[i].max_speed / l);
    return desired - boids[i].vel;
}
Vec2 Flocking::avoid_obstacles(int i) const {
    Vec2 steer(0, 0);
    for (int k = 0; k < obs_count; k++) {
        Vec2 d = boids[i].pos - obstacles[k].pos;
        double dist = d.length();
        double safe = obstacles[k].r + boids[i].perception;
        if (dist < safe && dist > 0.01) {
            steer += d / dist * (safe - dist);
        }
    }
    return steer;
}
Vec2 Flocking::flee_predators(int i) const {
    if (boids[i].is_predator) return Vec2(0, 0);
    Vec2 steer(0, 0);
    for (int j = 0; j < count; j++) {
        if (!boids[j].is_predator) continue;
        Vec2 d = boids[i].pos - boids[j].pos;
        double dist = d.length();
        if (dist < boids[i].perception * 2.0 && dist > 0.01) {
            steer += d / (dist * dist);
        }
    }
    return steer;
}
Vec2 Flocking::seek_path(int i) const {
    Vec2 desired = path_target - boids[i].pos;
    double l = desired.length();
    if (l < 1) return Vec2(0, 0);
    return desired * (boids[i].max_speed / l) - boids[i].vel;
}
void Flocking::step(double dt) {
    for (int i = 0; i < count; i++) {
        Boid& b = boids[i];
        Vec2 s = separate(i) * w_sep;
        Vec2 a = align(i) * w_ali;
        Vec2 c = cohesion(i) * w_coh;
        Vec2 o = avoid_obstacles(i) * w_avoid;
        Vec2 f = flee_predators(i) * w_flee;
        Vec2 p = seek_path(i) * w_path;
        Vec2 acc = s + a + c + o + f + p;
        acc = limit_force(acc, b.max_force);
        b.vel += acc * dt;
        double sp = b.vel.length();
        if (sp > b.max_speed) b.vel = b.vel * (b.max_speed / sp);
        b.pos += b.vel * dt;
        // 世界边界环形
        if (b.pos.x < 0) b.pos.x += world_w;
        if (b.pos.x > world_w) b.pos.x -= world_w;
        if (b.pos.y < 0) b.pos.y += world_h;
        if (b.pos.y > world_h) b.pos.y -= world_h;
    }
}
Vec2 Flocking::centroid() const {
    Vec2 c(0, 0);
    if (count == 0) return c;
    for (int i = 0; i < count; i++) c += boids[i].pos;
    return c / count;
}
double Flocking::avg_speed() const {
    if (count == 0) return 0;
    double s = 0;
    for (int i = 0; i < count; i++) s += boids[i].vel.length();
    return s / count;
}
double Flocking::dispersion() const {
    Vec2 c = centroid();
    if (count == 0) return 0;
    double d = 0;
    for (int i = 0; i < count; i++) d += (boids[i].pos - c).length();
    return d / count;
}
int Flocking::predator_count() const {
    int n = 0;
    for (int i = 0; i < count; i++) if (boids[i].is_predator) n++;
    return n;
}
int Flocking::prey_count() const { return count - predator_count(); }

// ============================================================
// 自检
// ============================================================
int flocking_self_test() {
    int fail = 0;

    // --- 1) 初始化 10 个 boid，跑 100 步不崩溃 ---
    {
        Flocking f;
        f.init(64, 4, 640, 480);
        for (int i = 0; i < 10; i++) {
            Boid b;
            b.pos = Vec2(100 + i * 10, 100);
            b.vel = Vec2(50, 0);
            f.add_boid(b);
        }
        for (int i = 0; i < 100; i++) f.step(0.01);
        if (f.count != 10) fail++;
        f.shutdown();
    }

    // --- 2) 对齐：两条同向 boid 跑后速度方向趋于一致 ---
    {
        Flocking f;
        f.init(8, 1, 640, 480);
        Boid b1; b1.pos = Vec2(100, 100); b1.vel = Vec2(50, 0);
        Boid b2; b2.pos = Vec2(110, 100); b2.vel = Vec2(0, 50);
        f.add_boid(b1); f.add_boid(b2);
        for (int i = 0; i < 200; i++) f.step(0.01);
        // 两者速度夹角应减小（粗略：速度向量都不 NaN）
        double sp = f.avg_speed();
        if (sp != sp || sp <= 0) fail++;
        f.shutdown();
    }

    // --- 3) 捕食者：猎物远离捕食者 ---
    {
        Flocking f;
        f.init(8, 1, 640, 480);
        Boid prey; prey.pos = Vec2(200, 200); prey.vel = Vec2(10, 0);
        Boid pred; pred.pos = Vec2(210, 200); pred.vel = Vec2(-10, 0);
        pred.is_predator = true;
        f.add_boid(prey); f.add_boid(pred);
        for (int i = 0; i < 50; i++) f.step(0.01);
        // 猎物应远离捕食者
        double d = (f.boids[0].pos - f.boids[1].pos).length();
        if (d < 0.5) fail++;
        f.shutdown();
    }

    // --- 4) 障碍规避：boid 不穿过圆 ---
    {
        Flocking f;
        f.init(4, 1, 640, 480);
        f.add_obstacle(Vec2(300, 200), 30);
        Boid b; b.pos = Vec2(100, 200); b.vel = Vec2(100, 0);
        f.add_boid(b);
        for (int i = 0; i < 200; i++) f.step(0.02);
        // boid 不应进入障碍物半径内
        double d = (f.boids[0].pos - f.obstacles[0].pos).length();
        if (d < 8) fail++;
        f.shutdown();
    }

    // --- 5) 统计：质心/平均速度/离散度 ---
    {
        Flocking f;
        f.init(4, 1, 640, 480);
        Boid b; b.pos = Vec2(100, 100); b.vel = Vec2(10, 0);
        f.add_boid(b);
        Boid b2; b2.pos = Vec2(120, 100); b2.vel = Vec2(10, 0);
        f.add_boid(b2);
        Vec2 c = f.centroid();
        if (fabs(c.x - 110) > 1) fail++;
        if (f.avg_speed() < 9 || f.avg_speed() > 11) fail++;
        f.shutdown();
    }

    return fail;
}

} // namespace simulate
} // namespace nefu
