// ============================================================================
// nefuOS 光线追踪引擎 —— rtstats: 渲染统计
// ----------------------------------------------------------------------------
// 统计渲染性能/正确性：
//   - 投射射线总数
//   - 命中/未命中计数
//   - BVH 节点访问数
//   - 阴影射线数
//   - 平均每像素时间（tick）
// 供 UI 显示与性能调优。
// ============================================================================
#pragma once
#include "rtmath.h"

namespace nefu {
namespace raytrace {

struct RTStats {
    long long rays_cast;
    long long rays_hit;
    long long rays_miss;
    long long shadow_rays;
    long long bvh_nodes_visited;
    long long prim_tests;
    int render_ms;

    RTStats() { clear(); }
    void clear() {
        rays_cast = rays_hit = rays_miss = shadow_rays = 0;
        bvh_nodes_visited = prim_tests = 0;
        render_ms = 0;
    }
    rtfx hit_ratio() const {
        if (rays_cast <= 0) return 0;
        return rt_div((rtfx)rays_hit, (rtfx)rays_cast);
    }
    rtfx shadow_block_ratio() const {
        if (shadow_rays <= 0) return 0;
        return rt_div((rtfx)(shadow_rays - (rays_cast - rays_hit)), (rtfx)shadow_rays);
    }
};

int rtstats_self_test();

} // namespace raytrace
} // namespace nefu
