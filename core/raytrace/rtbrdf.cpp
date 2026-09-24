// ============================================================================
// nefuOS 光线追踪引擎 —— rtbrdf 实现（Q16.16 定点，GGX）
// ============================================================================
#include "rtbrdf.h"

namespace nefu {
namespace raytrace {

rtfx GGX::ndf(const RTVec3& n, const RTVec3& h) const {
    rtfx ndoth = n.dot(h);
    if (ndoth <= 0) return 0;
    rtfx a = rt_mul(roughness, roughness);
    rtfx a2 = rt_mul(a, a);
    rtfx d = rt_mul(ndoth, ndoth) * (a2 - RT_ONE) + RT_ONE;
    return rt_div(a2, rt_mul(RT_PI, rt_mul(d, d)));
}

rtfx GGX::g1(const RTVec3& n, const RTVec3& v) const {
    rtfx ndotv = n.dot(v);
    if (ndotv <= 0) return 0;
    rtfx a = rt_mul(roughness, roughness);
    rtfx k = rt_mul(a, fx::fxf(1,2));   // 直接光照用 (a^2)/2
    return rt_div(ndotv, ndotv * (RT_ONE - k) + k);
}

rtfx GGX::geometry(const RTVec3& n, const RTVec3& v, const RTVec3& l) const {
    return rt_mul(g1(n, v), g1(n, l));
}

RTVec3 GGX::fresnel(rtfx cos_theta, const RTVec3& f0) const {
    rtfx t = RT_ONE - cos_theta;
    // Schlick: F = F0 + (1-F0)*(1-cos)^5
    rtfx t2 = rt_mul(t, t);
    rtfx t5 = rt_mul(rt_mul(t2, t2), t);
    return f0 + (RTVec3(RT_ONE,RT_ONE,RT_ONE) - f0) * t5;
}

RTVec3 GGX::sample(const RTVec3& n, RTRng& rng) const {
    // 余弦重要性采样微表面
    rtfx u1 = rng.next_fx();
    rtfx u2 = rng.next_fx();
    rtfx a = rt_mul(roughness, roughness);
    rtfx phi = rt_mul(u2, RT_2PI);
    rtfx cos_theta = rt_sqrt(rt_div(RT_ONE - u1, rt_mul(rt_mul(a - RT_ONE, u1), rt_itofx(-1)) + RT_ONE));
    rtfx sin_theta = rt_sqrt(RT_ONE - rt_mul(cos_theta, cos_theta));
    RTVec3 h = RTVec3(rt_mul(sin_theta, fx::fx_cos(phi)),
                      rt_mul(sin_theta, fx::fx_sin(phi)),
                      cos_theta);
    // 转到世界系
    RTVec3 up = (rt_abs(n.y) < RT_HALF) ? RTVec3(0,1,0) : RTVec3(1,0,0);
    RTVec3 T = n.cross(up).normalized();
    RTVec3 B = n.cross(T);
    return T * h.x + B * h.y + n * h.z;
}

rtfx GGX::pdf(const RTVec3& n, const RTVec3& h) const {
    rtfx ndoth = n.dot(h);
    if (ndoth <= 0) return 0;
    return rt_mul(ndf(n, h), ndoth);
}

int rtbrdf_self_test() {
    int fail = 0;
    GGX ggx(fx::fxf(2,10), 1);
    RTVec3 n = RTVec3(0, RT_ONE, 0);
    // 1. NDF 在 h=n 时最大
    {
        rtfx d0 = ggx.ndf(n, n);
        rtfx d1 = ggx.ndf(n, RTVec3(fx::fxf(1,2),fx::fxf(1,2),0).normalized());
        if (d0 < d1) fail++;
    }
    // 2. Fresnel：正入射 = f0
    {
        RTVec3 f0(fx::fxf(4,10),fx::fxf(4,10),fx::fxf(4,10));
        RTVec3 f = ggx.fresnel(RT_ONE, f0);
        if (!rt_near(f.x, f0.x, 300)) fail++;
    }
    // 3. sample 方向应在上半球
    {
        RTRng rng(5);
        for (int i = 0; i < 10; i++) {
            RTVec3 d = ggx.sample(n, rng);
            if (d.y <= 0) fail++;
        }
    }
    // 4. geometry 在 [0,1]
    {
        rtfx g = ggx.geometry(n, RTVec3(0,1,0), RTVec3(0,1,0));
        if (g < 0 || g > RT_ONE + 100) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
