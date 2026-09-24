// ============================================================================
// nefuOS 光线追踪引擎 —— rtonemap 实现（Q16.16 定点）
// ============================================================================
#include "rtonemap.h"

namespace nefu {
namespace raytrace {

RTVec3 tonemap_reinhard(const RTVec3& c) {
    return RTVec3(rt_div(c.x, RT_ONE + c.x),
                  rt_div(c.y, RT_ONE + c.y),
                  rt_div(c.z, RT_ONE + c.z));
}

RTVec3 tonemap_aces(const RTVec3& c) {
    // Narkowicz ACES 近似：clamp((x*(2.51x+0.03))/(x*(2.43x+0.59)+0.14))
    auto f = [](rtfx x) -> rtfx {
        rtfx num = rt_mul(x, rt_mul(rt_itofx(2), x) + fx::fxf(3,100));
        rtfx den = rt_mul(x, rt_mul(rt_itofx(2), x) + fx::fxf(59,100)) + fx::fxf(14,100);
        rtfx r = rt_div(num, den);
        return rt_clamp(r, 0, RT_ONE);
    };
    return RTVec3(f(c.x), f(c.y), f(c.z));
}

RTVec3 tonemap_filmic(const RTVec3& c) {
    // 简化 filmic：软高光压缩
    auto f = [](rtfx x) -> rtfx {
        rtfx a = fx::fxf(1,2);
        rtfx b = rt_mul(x, rt_itofx(2));
        rtfx r = rt_div(b, RT_ONE + rt_mul(a, b));
        return rt_clamp(r, 0, RT_ONE);
    };
    return RTVec3(f(c.x), f(c.y), f(c.z));
}

RTVec3 color_exposure(const RTVec3& c, rtfx exposure) {
    return RTVec3(rt_mul(c.x, exposure), rt_mul(c.y, exposure), rt_mul(c.z, exposure));
}

RTVec3 color_srgb_encode(const RTVec3& c) {
    // 近似 1/2.2：用 sqrt 两次叠加
    rtfx g1 = rt_sqrt(rt_clamp(c.x, 0, RT_ONE));
    rtfx g2 = rt_sqrt(rt_clamp(c.y, 0, RT_ONE));
    rtfx g3 = rt_sqrt(rt_clamp(c.z, 0, RT_ONE));
    return RTVec3(g1, g2, g3);
}

rtfx color_luma(const RTVec3& c) {
    return rt_mul(c.x, fx::fxf(3,10))
         + rt_mul(c.y, fx::fxf(6,10))
         + rt_mul(c.z, fx::fxf(1,10));
}

RTVec3 color_saturate(const RTVec3& c) {
    return RTVec3(rt_clamp(c.x, 0, RT_ONE),
                  rt_clamp(c.y, 0, RT_ONE),
                  rt_clamp(c.z, 0, RT_ONE));
}

int rtonemap_self_test() {
    int fail = 0;
    // 1. Reinhard：输入 10 -> 输出 < 1
    {
        RTVec3 c = tonemap_reinhard(RTVec3(rt_itofx(9), rt_itofx(9), rt_itofx(9)));
        if (c.x >= RT_ONE) fail++;
    }
    // 2. ACES 输出在 [0,1]
    {
        RTVec3 c = tonemap_aces(RTVec3(rt_itofx(5), rt_itofx(2), 0));
        if (c.x < 0 || c.x > RT_ONE) fail++;
    }
    // 3. 曝光加倍
    {
        RTVec3 c = color_exposure(RTVec3(RT_HALF,RT_HALF,RT_HALF), rt_itofx(2));
        if (!rt_near(c.x, RT_ONE, 300)) fail++;
    }
    // 4. luma
    {
        rtfx l = color_luma(RTVec3(RT_ONE,0,0));
        if (l <= 0) fail++;
    }
    // 5. saturate
    {
        RTVec3 c = color_saturate(RTVec3(rt_itofx(2), -rt_itofx(1), RT_HALF));
        if (c.x != RT_ONE) fail++;
        if (c.y != 0) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
