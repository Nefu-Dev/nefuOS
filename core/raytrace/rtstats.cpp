// ============================================================================
// nefuOS 光线追踪引擎 —— rtstats 实现
// ============================================================================
#include "rtstats.h"

namespace nefu {
namespace raytrace {

int rtstats_self_test() {
    int fail = 0;
    RTStats s;
    // 1. 初始全零
    if (s.rays_cast != 0) fail++;
    // 2. hit_ratio
    {
        s.rays_cast = 100; s.rays_hit = 50;
        rtfx r = s.hit_ratio();
        if (!rt_near(r, fx::fxf(1,2), 300)) fail++;
    }
    // 3. clear
    s.clear();
    if (s.rays_cast != 0) fail++;
    // 4. shadow ratio 不爆
    {
        s.shadow_rays = 10; s.rays_cast = 20; s.rays_hit = 15;
        rtfx r = s.shadow_block_ratio();
        (void)r;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
