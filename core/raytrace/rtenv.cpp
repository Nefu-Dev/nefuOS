// ============================================================================
// nefuOS 光线追踪引擎 —— rtenv 实现（Q16.16 定点）
// ============================================================================
#include "rtenv.h"

namespace nefu {
namespace raytrace {

RTVec3 RTEnv::lookup(const RTVec3& dir) const {
    RTVec3 d = dir.normalized();
    rtfx t = rt_clamp((d.y + RT_ONE) / rt_itofx(2), 0, RT_ONE);
    RTVec3 sky;
    if (d.y >= 0) {
        sky = rt_lerp(horizon, top, t);
    } else {
        sky = rt_lerp(horizon, bottom, RT_ONE - t);
    }
    // 太阳圆盘
    rtfx cosang = d.dot(sun_dir);
    if (cosang > sun_size) {
        sky = sky + sun_color;
    }
    return sky * intensity;
}

rtfx RTEnv::pdf(const RTVec3& dir) const {
    rtfx cosang = dir.normalized().dot(sun_dir);
    if (cosang > sun_size) return rt_div(RT_ONE, RT_2PI);
    return rt_div(RT_ONE, RT_2PI);
}

RTVec3 RTEnv::sample(RTRng& rng) const {
    // 均匀半球采样
    rtfx u1 = rng.next_fx();
    rtfx u2 = rng.next_fx();
    rtfx r = rt_sqrt(u1);
    rtfx phi = rt_mul(u2, RT_2PI);
    return RTVec3(rt_mul(r, fx::fx_cos(phi)),
                  rt_sqrt(RT_ONE - u1),
                  rt_mul(r, fx::fx_sin(phi)));
}

int rtenv_self_test() {
    int fail = 0;
    RTEnv env;
    // 1. 朝上方向应得到天空色
    {
        RTVec3 c = env.lookup(RTVec3(0, RT_ONE, 0));
        if (c.z <= 0) fail++;
    }
    // 2. 太阳方向应高亮
    {
        RTVec3 c = env.lookup(env.sun_dir);
        RTVec3 c2 = env.lookup(RTVec3(0, -RT_ONE, 0));
        if (c.z <= c2.z) fail++;
    }
    // 3. sample 方向单位长
    {
        RTRng rng(5);
        RTVec3 d = env.sample(rng);
        if (rt_abs(d.length() - RT_ONE) > fx::fxf(1,10)) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
