// ============================================================================
// nefuOS 光线追踪引擎 —— rtsky 实现
// ============================================================================
#include "rtsky.h"

namespace nefu {
namespace raytrace {

RTVec3 RTSky::sample(const RTVec3& dir) const {
    rtfx t = rt_clamp((dir.y + RT_ONE) / rt_itofx(2), 0, RT_ONE);
    RTVec3 c = rt_lerp(horizon, zenith, t);
    // 太阳高光
    rtfx cosang = rt_max(0, dir.dot(sun_dir));
    rtfx glow = rt_mul(cosang, cosang);
    c = c + RTVec3(RT_ONE, fx::fxf(9,10), fx::fxf(7,10)) * glow;
    return c;
}

int rtsky_self_test() {
    int fail = 0;
    RTSky sky;
    // 1. 朝上应得天空色
    {
        RTVec3 c = sky.sample(RTVec3(0, RT_ONE, 0));
        if (c.z <= 0) fail++;
    }
    // 2. 朝太阳应更亮
    {
        RTVec3 c = sky.sample(sky.sun_dir);
        RTVec3 n = sky.sample(RTVec3(0,0,RT_ONE));
        if (c.x < n.x) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
