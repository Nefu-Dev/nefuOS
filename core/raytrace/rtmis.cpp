// ============================================================================
// nefuOS 光线追踪引擎 —— rtmis 实现（Q16.16 定点）
// ============================================================================
#include "rtmis.h"

namespace nefu {
namespace raytrace {

rtfx mis_power_heuristic(rtfx pdf_brdf, rtfx pdf_light) {
    if (pdf_brdf <= 0 && pdf_light <= 0) return 0;
    rtfx pb = rt_mul(pdf_brdf, pdf_brdf);
    rtfx pl = rt_mul(pdf_light, pdf_light);
    return rt_div(pb, pb + pl);
}

rtfx mis_balance_heuristic(rtfx pdf, int n) {
    if (n <= 0) return 0;
    return rt_div(pdf, pdf * rt_itofx(n));
}

RTVec3 mis_combine(const RTVec3& brdf_li, rtfx pdf_brdf,
                   const RTVec3& light_li, rtfx pdf_light) {
    rtfx wb = mis_power_heuristic(pdf_brdf, pdf_light);
    rtfx wl = mis_power_heuristic(pdf_light, pdf_brdf);
    return brdf_li * wb + light_li * wl;
}

int rtmis_self_test() {
    int fail = 0;
    // 1. pdf 相等时权重各 0.5
    {
        rtfx w = mis_power_heuristic(rt_itofx(1), rt_itofx(1));
        if (!rt_near(w, fx::fxf(1,2), 300)) fail++;
    }
    // 2. brdf pdf 高时偏向 brdf
    {
        rtfx w = mis_power_heuristic(rt_itofx(10), rt_itofx(1));
        if (w < fx::fxf(9,10)) fail++;
    }
    // 3. combine 不爆
    {
        RTVec3 c = mis_combine(RTVec3(RT_ONE,0,0), rt_itofx(1),
                               RTVec3(0,RT_ONE,0), rt_itofx(1));
        if (c.x <= 0 || c.y <= 0) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
