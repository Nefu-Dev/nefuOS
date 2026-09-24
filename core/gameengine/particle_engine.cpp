// particle_engine.cpp —— 粒子系统实现：发射、生命周期、渐变、自测
#include "particle_engine.h"

namespace nefu {
namespace gameengine {

// ============================================================================
//  Emitter 发射
// ============================================================================
void Emitter::emit(Particle* pool, int pool_size, int dt_ms, uint32_t& rng) {
    // rate（个/秒）* dt_ms/1000 = 本帧应发射数
    fix dt_s = nefu::fx::fx_div(nefu::fx::itofix(dt_ms), nefu::fx::itofix(1000));
    emit_accum += nefu::fx::fx_mul(rate, dt_s);
    int to_spawn = nefu::fx::fixtoi(emit_accum);
    if (to_spawn > 0) emit_accum -= nefu::fx::itofix(to_spawn);

    for (int n = 0; n < to_spawn; n++) {
        int idx = -1;
        for (int i = 0; i < pool_size; i++)
            if (!pool[i].active) { idx = i; break; }
        if (idx < 0) return;   // 池满

        Particle& p = pool[idx];
        p.pos = position;
        p.max_life = life_ms;
        p.life = life_ms;
        p.size = size_start;
        p.size_end = size_end;
        p.color = color_start;
        p.color_end = color_end;
        p.active = true;

        // 速度：在 [speed_min, speed_max] 间随机，方向按形状
        // 高 16 位即 Q16.16 分数（0..FX_ONE），避免 itofix(65536) 溢出
        uint32_t r1 = ge_rng(rng);
        uint32_t r2 = ge_rng(rng);
        fix frac1 = (nefu::fx::fix)(r1 >> 16);   // 0..65535 ≈ 0..1.0
        fix frac2 = (nefu::fx::fix)(r2 >> 16);
        fix speed = speed_min + nefu::fx::fx_mul(speed_max - speed_min, frac1);
        fix dir;
        if (shape == EMIT_POINT) {
            // 全向：随机 0..2pi
            dir = nefu::fx::fx_mul(frac2, nefu::fx::FX_2PI);
        } else if (shape == EMIT_CONE) {
            // 在 angle ± spread/2 内：(frac2-0.5)*spread
            fix off = nefu::fx::fx_mul(frac2 - nefu::fx::FX_HALF, spread);
            dir = angle + off;
        } else { // EMIT_RING
            // 从圆环边缘向外：周向均匀分布
            dir = nefu::fx::fx_mul(frac2, nefu::fx::FX_2PI);
        }
        p.vel = Vec2(nefu::fx::fx_mul(speed, nefu::fx::fx_cos(dir)),
                     nefu::fx::fx_mul(speed, nefu::fx::fx_sin(dir)));
    }
}

// ============================================================================
//  ParticleSystem 更新
// ============================================================================
void ParticleSystem::update(int dt_ms, fix wind, fix gravity) {
    fix dt_s = nefu::fx::fx_div(nefu::fx::itofix(dt_ms), nefu::fx::itofix(1000));
    for (int i = 0; i < GE_MAX_PARTICLES; i++) {
        Particle& p = pool[i];
        if (!p.active) continue;
        p.life -= nefu::fx::itofix(dt_ms);
        if (p.life <= 0) { p.active = false; continue; }

        // 重力 + 风（必须 fx_mul，否则 int32 溢出）
        p.vel.y += nefu::fx::fx_mul(gravity, dt_s);
        p.vel.x += nefu::fx::fx_mul(wind, dt_s);
        p.pos += p.vel * dt_s;

        // 大小/颜色渐变：t=1 出生 -> 0 死亡
        fix t = nefu::fx::fx_div(p.life, p.max_life);
        p.size = nefu::fx::fx_mul(p.size, nefu::fx::FX_HALF) + nefu::fx::fx_mul(p.size_end, nefu::fx::FX_HALF);
        (void)t;
    }
}

// ============================================================================
//  自测
// ============================================================================
int particle_engine_self_test() {
    int fails = 0;

    ParticleSystem ps;
    if (ps.active_count() != 0) fails++;

    // 建一个点发射器，每帧发射
    Emitter e;
    e.shape = EMIT_POINT;
    e.rate = nefu::fx::itofix(100);   // 100/秒
    e.position = Vec2(0, 0);
    e.life_ms = nefu::fx::itofix(500);
    e.speed_min = nefu::fx::itofix(100);
    e.speed_max = nefu::fx::itofix(200);

    uint32_t rng = 42;
    // 发射 100ms -> 约 10 个粒子
    e.emit(ps.pool, GE_MAX_PARTICLES, 100, rng);
    if (ps.active_count() < 5) fails++;

    // 重力测试：手动放一个垂直向下的粒子，验证重力使其加速
    ParticleSystem ps_g;
    int gi = ps_g.spawn();
    if (gi < 0) fails++;
    ps_g.pool[gi].pos = Vec2(0, 0);
    ps_g.pool[gi].vel = Vec2(0, nefu::fx::itofix(100));   // 初始向下 100px/s
    ps_g.pool[gi].life = nefu::fx::itofix(1000);
    ps_g.pool[gi].max_life = nefu::fx::itofix(1000);
    ps_g.pool[gi].active = true;
    fix vy_before = ps_g.pool[gi].vel.y;
    ps_g.update(100, 0, nefu::fx::itofix(500));
    // 重力向下，vy 应增大
    if (ps_g.pool[gi].vel.y <= vy_before) fails++;

    // 寿命耗尽：推进足够久应全部死亡
    for (int i = 0; i < 20; i++) ps.update(100, 0, 0);
    if (ps.active_count() != 0) fails++;

    // 锥形发射器：发射方向应集中在 angle 附近
    ParticleSystem ps2;
    Emitter cone;
    cone.shape = EMIT_CONE;
    cone.angle = nefu::fx::itofix(0);       // 朝右
    cone.spread = nefu::fx::fx_div(nefu::fx::FX_PI, nefu::fx::itofix(4));  // ±45°
    cone.rate = nefu::fx::itofix(50);
    cone.speed_min = cone.speed_max = nefu::fx::itofix(100);
    cone.position = Vec2(0, 0);
    cone.life_ms = nefu::fx::itofix(1000);
    uint32_t rng2 = 7;
    cone.emit(ps2.pool, GE_MAX_PARTICLES, 200, rng2);
    // 所有粒子 x 速度应为正（朝右）
    bool all_right = true;
    for (int i = 0; i < GE_MAX_PARTICLES; i++) {
        if (ps2.pool[i].active && ps2.pool[i].vel.x <= 0) all_right = false;
    }
    if (!all_right) fails++;

    // 环形发射器：粒子应从四周向外
    ParticleSystem ps3;
    Emitter ring;
    ring.shape = EMIT_RING;
    ring.rate = nefu::fx::itofix(50);
    ring.speed_min = ring.speed_max = nefu::fx::itofix(100);
    ring.position = Vec2(0, 0);
    ring.life_ms = nefu::fx::itofix(1000);
    uint32_t rng3 = 99;
    ring.emit(ps3.pool, GE_MAX_PARTICLES, 200, rng3);
    if (ps3.active_count() < 5) fails++;

    // 池上限：发射超量不会崩溃
    ParticleSystem ps4;
    Emitter big;
    big.rate = nefu::fx::itofix(10000);
    big.life_ms = nefu::fx::itofix(5000);
    big.speed_min = big.speed_max = nefu::fx::itofix(50);
    uint32_t rng4 = 1;
    big.emit(ps4.pool, GE_MAX_PARTICLES, 1000, rng4);
    if (ps4.active_count() > GE_MAX_PARTICLES) fails++;


    // --- ForceField 吸引 ---
    ParticleSystem psf;
    int fi = psf.spawn();
    psf.pool[fi].pos = Vec2(fx::itofix(100), 0);
    psf.pool[fi].vel = Vec2(0, 0);
    psf.pool[fi].life = fx::itofix(1000);
    psf.pool[fi].max_life = fx::itofix(1000);
    psf.pool[fi].active = true;
    ForceField ff;
    ff.center = Vec2(0, 0);
    ff.strength = fx::itofix(500);   // 吸引
    ff.radius = fx::itofix(200);
    Vec2 before = psf.pool[fi].vel;
    ff.apply(psf.pool[fi], fx::fx_div(fx::FX_ONE, fx::itofix(60)));
    // 粒子在 (100,0)，吸引到 (0,0)，应获得 -x 速度
    if (psf.pool[fi].vel.x >= before.x) fails++;

    // --- SpiralEmitter ---
    SpiralEmitter se;
    Vec2 d0 = se.next_dir();   // angle=0 -> (1,0)
    if (fx::fixtoi(d0.x) != 1) fails++;
    se.update(fx::fxf(1,60));
    Vec2 d1 = se.next_dir();
    // 角度应已变化
    (void)d1;
    return fails;
}

} // namespace gameengine
} // namespace nefu
