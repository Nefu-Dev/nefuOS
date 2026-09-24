// particle_engine.h —— 粒子系统：发射器、生命周期、颜色/大小/速度渐变、重力/风场、环形/锥形/点发射器
//
// 纯逻辑在 .cpp；渲染 inline 在头文件（未调用不产生符号）。
#pragma once

#include <stdint.h>
#include "../klib/klib.h"
#include "../gui/gfx.h"
#include "ge_math.h"

namespace nefu {
namespace gameengine {

const int GE_MAX_PARTICLES = 256;

// 发射器形状
enum EmitterShape {
    EMIT_POINT = 0,   // 点发射
    EMIT_CONE,        // 锥形（指定方向 + 散布角）
    EMIT_RING,        // 环形（从圆周边缘向外）
};

// ============================================================================
//  Particle —— 单个粒子
// ============================================================================
struct Particle {
    Vec2     pos;
    Vec2     vel;
    fix      life;          // 剩余寿命（毫秒）
    fix      max_life;
    fix      size;          // 当前大小（像素）
    fix      size_end;      // 死亡时大小
    uint32_t color;
    uint32_t color_end;
    bool     active;

    Particle() : life(0), max_life(0), size(fx::FX_ONE), size_end(0),
                 color(0x00FFFFFF), color_end(0x00000000), active(false) {}
};

// ============================================================================
//  Emitter —— 粒子发射器
// ============================================================================
struct Emitter {
    Vec2      position;
    EmitterShape shape;
    fix       angle;          // 主方向（锥形用）
    fix       spread;         // 散布角（弧度，锥形）
    fix       rate;           // 每秒发射数
    fix       speed_min, speed_max;
    fix       life_ms;        // 粒子寿命
    fix       size_start, size_end;   // 大小渐变
    uint32_t  color_start, color_end; // 颜色渐变（出生->死亡）
    fix       gravity;        // 重力影响
    fix       wind;           // 水平风
    fix       emit_accum;     // 发射时间累积

    Emitter() : shape(EMIT_POINT), angle(0), spread(fx::FX_PI_2),
                rate(fx::itofix(30)), speed_min(fx::itofix(50)),
                speed_max(fx::itofix(150)), life_ms(fx::itofix(800)),
                size_start(fx::itofix(4)), size_end(0),
                color_start(0x00FFFFFF), color_end(0x00FF0000),
                gravity(fx::itofix(300)), wind(0), emit_accum(0) {}

    // 在 dt_ms 内发射粒子到 out 池
    void emit(Particle* pool, int pool_size, int dt_ms,
              uint32_t& rng_state);
};

// ============================================================================
//  ParticleSystem —— 粒子池 + 更新
// ============================================================================
struct ParticleSystem {
    Particle pool[GE_MAX_PARTICLES];

    ParticleSystem() { reset(); }

    void reset() {
        for (int i = 0; i < GE_MAX_PARTICLES; i++) pool[i].active = false;
    }

    // 找一个空闲粒子槽
    int spawn() {
        for (int i = 0; i < GE_MAX_PARTICLES; i++)
            if (!pool[i].active) return i;
        return -1;
    }

    // 推进所有粒子；dt_ms 毫秒
    void update(int dt_ms, fix wind, fix gravity);

    int active_count() const {
        int n = 0;
        for (int i = 0; i < GE_MAX_PARTICLES; i++) if (pool[i].active) n++;
        return n;
    }
};

// 简单 xorshift RNG（供发射器用，独立于 games2）
inline uint32_t ge_rng(uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}

// ============================================================================
//  渲染（inline）：画粒子为小方块
// ============================================================================
inline void draw_particles(Surface& dst, const ParticleSystem& ps) {
    for (int i = 0; i < GE_MAX_PARTICLES; i++) {
        const Particle& p = ps.pool[i];
        if (!p.active) continue;
        int sz = fx::fixtoi(p.size);
        if (sz < 1) sz = 1;
        int x = p.pos.to_ix() - sz / 2;
        int y = p.pos.to_iy() - sz / 2;
        // 简单 alpha：按寿命比例淡化（画矩形）
        for (int yy = 0; yy < sz; yy++)
            for (int xx = 0; xx < sz; xx++) {
                int px = x + xx, py = y + yy;
                if (px < 0 || py < 0 || px >= dst.width || py >= dst.height) continue;
                dst.setpx(px, py, p.color);
            }
    }
}

// ============================================================================
//  自测
// ============================================================================

// ============================================================================
//  ForceField —— 空间力场（吸引/排斥/漩涡）
// ============================================================================
enum FieldType {
    FIELD_POINT = 0,    // 吸引或排斥
    FIELD_WIND,         // 恒定方向风
    FIELD_VORTEX,       // 切向漩涡
};

struct ForceField {
    Vec2       center;
    fix        strength;     // 强度（正负=吸/斥）
    fix        radius;       // 影响半径
    FieldType  type;
    Vec2       wind_dir;    // FIELD_WIND 用

    ForceField() : strength(fx::itofix(200)), radius(fx::itofix(200)),
                   type(FIELD_POINT) {}

    // 对一个粒子施加力（直接修改速度）
    void apply(Particle& p, fix dt_s) {
        if (type == FIELD_WIND) {
            p.vel.x += wind_dir.x * dt_s;
            p.vel.y += wind_dir.y * dt_s;
            return;
        }
        Vec2 d = center - p.pos;
        fix dist = d.len();
        if (dist > radius || dist == 0) return;
        fix falloff = nefu::fx::fx_div(radius - dist, radius);  // 1->0
        fix f = nefu::fx::fx_mul(strength, falloff) * dt_s;
        Vec2 dir = d / dist;   // 单位向量
        p.vel.x += nefu::fx::fx_mul(dir.x, f);
        p.vel.y += nefu::fx::fx_mul(dir.y, f);
    }
};

// ============================================================================
//  SpiralEmitter —— 螺旋发射器（绕中心旋转喷出）
// ============================================================================
struct SpiralEmitter {
    Vec2 center;
    fix  angle;
    fix  speed;
    fix  radius;
    fix  angular_speed;
    int  particles_per_sec;

    SpiralEmitter() : angle(0), speed(fx::itofix(100)), radius(fx::itofix(50)),
                      angular_speed(fx::itofix(2)), particles_per_sec(60) {}

    void update(fix dt_s) {
        angle += fx::fx_mul(angular_speed, dt_s);
    }
    // 生成一个粒子的初速度方向
    Vec2 next_dir() const {
        return Vec2(fx::fx_cos(angle), fx::fx_sin(angle));
    }
};
int particle_engine_self_test();

} // namespace gameengine
} // namespace nefu
