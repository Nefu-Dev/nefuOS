// ============================================================================
// nefuOS 光线追踪引擎 —— pathtracer: 光线/路径追踪核心
// ----------------------------------------------------------------------------
// 混合 Whitted 与蒙特卡洛路径追踪：
//   - 直接光照：逐光源 NEE（Next Event Estimation）+ 阴影射线
//   - 间接光照：材质散射方向递归追踪（余弦重要性采样）
//   - 俄罗斯轮盘：深度较大时概率终止，避免无界递归
//   - 背景：程序化天空渐变
// trace() 返回 Q16.16 RGB radiance（分量 [0, 若干]，tonemap 在外做）。
// ============================================================================
#pragma once
#include "rtmath.h"
#include "primitives.h"
#include "materials.h"
#include "lights.h"
#include "bvh.h"

namespace nefu {
namespace raytrace {

struct Pathtracer {
    int     max_depth;       // 最大递归深度
    rtfx    rr_threshold;    // 俄罗斯轮盘启用深度阈值
    RTVec3  sky_top;         // 背景天空色（上）
    RTVec3  sky_bot;         // 背景天空色（下）

    Pathtracer() : max_depth(6), rr_threshold(3) {
        sky_top = RTVec3(fx::fxf(1,2), fx::fxf(3,4), RT_ONE);
        sky_bot = RTVec3(fx::fxf(1,4), fx::fxf(1,4), fx::fxf(1,3));
    }

    // 背景：根据射线方向 y 分量在天空色间插值
    RTVec3 background(const RTRay& ray) const;

    // 核心：追踪一条射线，返回 radiance
    // bvh 用于加速求交，mats/lights 为场景表
    RTVec3 trace(const RTRay& ray, int depth, RTRng& rng,
                 const BVH& bvh, const Material* mats, int nmats,
                 const Light* lights, int nlights) const;

    // 判断阴影：从 p 朝 light_dir 走 light_dist 是否被遮挡
    bool occluded(const RTVec3& p, const RTVec3& dir, rtfx dist,
                  const BVH& bvh) const;

    // MIS 路径追踪：同时用 BRDF 采样和光源采样，按幂启发式加权
    RTVec3 trace_mis(const RTRay& ray, int depth, RTRng& rng,
                     const BVH& bvh, const Material* mats, int nmats,
                     const Light* lights, int nlights) const;
};

int pathtracer_self_test();

} // namespace raytrace
} // namespace nefu
