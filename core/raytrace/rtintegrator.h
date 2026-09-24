// ============================================================================
// nefuOS 光线追踪引擎 —— rtintegrator: 积分器族
// ----------------------------------------------------------------------------
// 把 trace() 抽象成可替换的积分器，便于对比：
//   - DirectLight: 仅直接光照（快，无反弹）
//   - Whitted:    经典 Whitted（直接 + 镜面反射/折射）
//   - Path:       蒙特卡洛路径追踪（含 NEE + RR）
//   - AmbientOcc: 环境光遮蔽采样
// 每个积分器都实现 L(ray) -> radiance。
// ============================================================================
#pragma once
#include "rtmath.h"
#include "primitives.h"
#include "materials.h"
#include "lights.h"
#include "bvh.h"

namespace nefu {
namespace raytrace {

struct IntegratorContext {
    const BVH*      bvh;
    const Material* mats;
    int             nmats;
    const Light*    lights;
    int             nlights;
    RTVec3          sky_top;
    RTVec3          sky_bot;
};

// 直接光照积分器：无反弹
RTVec3 integrate_direct(const RTRay& ray, RTRng& rng, const IntegratorContext& ctx);
// Whitted：直接 + 镜面递归
RTVec3 integrate_whitted(const RTRay& ray, int depth, RTRng& rng, const IntegratorContext& ctx);
// 路径追踪：NEE + 余弦采样
RTVec3 integrate_path(const RTRay& ray, int depth, RTRng& rng, const IntegratorContext& ctx);
// 环境光遮蔽：采样表面周围半球
RTVec3 integrate_ao(const RTRay& ray, RTRng& rng, const IntegratorContext& ctx, rtfx max_dist);

// 背景色
RTVec3 integrator_background(const RTRay& ray, const IntegratorContext& ctx);

int rtintegrator_self_test();

} // namespace raytrace
} // namespace nefu
