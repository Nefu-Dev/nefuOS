// ============================================================================
// nefuOS 光线追踪引擎 —— texture 实现（Q16.16 定点）
// ============================================================================
#include "texture.h"

namespace nefu {
namespace raytrace {

// 简易 2D 值噪声：哈希整数格点做平滑插值
static rtfx value_noise(rtfx x, rtfx y) {
    // 格点整数
    int xi = x >> 16, yi = y >> 16;
    rtfx xf = x & 0xFFFF, yf = y & 0xFFFF;
    // 平滑权重
    rtfx u = rt_mul(xf, rt_mul(xf, rt_itofx(3) - rt_mul(rt_itofx(2), xf)));
    rtfx v = rt_mul(yf, rt_mul(yf, rt_itofx(3) - rt_mul(rt_itofx(2), yf)));
    // 哈希
    auto hash = [](int a, int b) -> rtfx {
        uint32_t h = (uint32_t)(a * 374761393 + b * 668265263);
        h = (h ^ (h >> 13)) * 1274126177u;
        return (rtfx)((h & 0xFFFF));       // [0,1) Q16.16
    };
    rtfx v00 = hash(xi, yi), v10 = hash(xi + 1, yi);
    rtfx v01 = hash(xi, yi + 1), v11 = hash(xi + 1, yi + 1);
    rtfx a = v00 + rt_mul(u, v10 - v00);
    rtfx b = v01 + rt_mul(u, v11 - v01);
    return a + rt_mul(v, b - a);
}


// 分形布朗运动：叠加多倍频 value_noise，振幅逐次减半
static rtfx rt_fbm2(rtfx x, rtfx y, int octaves) {
    rtfx amp = RT_ONE;
    rtfx freq = RT_ONE;
    rtfx sum = 0;
    rtfx norm = 0;
    for (int i = 0; i < octaves; i++) {
        sum += rt_mul(value_noise(rt_mul(x, freq), rt_mul(y, freq)), amp);
        norm += amp;
        amp = rt_mul(amp, RT_HALF);
        freq = rt_mul(freq, rt_itofx(2));
    }
    return rt_div(sum, norm);
}
RTVec3 Texture::sample(const RTVec2& uv) const {
    switch (type) {
    case TEX_SOLID: return c1;
    case TEX_CHECKER: {
        rtfx s = scale;
        rtfx cx = rt_mul(uv.u, s), cy = rt_mul(uv.v, s);
        int ix = rt_fxtoi(cx), iy = rt_fxtoi(cy);
        return ((ix + iy) & 1) ? c1 : c2;
    }
    case TEX_GRADIENT:
        return rt_lerp(c1, c2, uv.u);
    case TEX_NOISE: {
        rtfx n = value_noise(rt_mul(uv.u, scale), rt_mul(uv.v, scale));
        return rt_lerp(c1, c2, n);
    }
    case TEX_IMAGE: {
        // 程序化条纹图像
        int band = rt_fxtoi(rt_mul(uv.u, rt_itofx(8)));
        return (band & 1) ? c1 : c2;
    }
    case TEX_FBM: {
        rtfx n = rt_fbm2(rt_mul(uv.u, scale), rt_mul(uv.v, scale), 5);
        return rt_lerp(c1, c2, n);
    }
    case TEX_MARBLE: {
        rtfx n = rt_fbm2(rt_mul(uv.u, scale), rt_mul(uv.v, scale), 4);
        rtfx vein = rt_div(RT_ONE, RT_ONE + rt_mul(rt_itofx(8), rt_mul(n, n)));
        return rt_lerp(c1, c2, vein);
    }
    case TEX_WOOD: {
        rtfx n = rt_fbm2(rt_mul(uv.u, scale), rt_mul(uv.v, scale), 4);
        rtfx ring = rt_abs(rt_mul(n, RT_2PI) - rt_itofx(2));
        return rt_lerp(c1, c2, rt_clamp(ring, 0, RT_ONE));
    }
    default: return c1;
    }
}

RTVec3 Texture::sample_world(const RTVec3& p) const {
    if (type == TEX_CHECKER || type == TEX_NOISE) {
        rtfx s = scale;
        rtfx n = value_noise(rt_mul(p.x, s), rt_mul(p.z, s));
        return rt_lerp(c1, c2, n);
    }
    return c1;
}

RTVec3 Texture::perturb_normal(const RTVec3& n, const RTVec3& p) const {
    if (type != TEX_BUMP) return n;
    rtfx h = value_noise(rt_mul(p.x, rt_itofx(4)), rt_mul(p.z, rt_itofx(4)));
    // 沿切向加一个由噪声高度决定的偏移
    RTVec3 up = (rt_abs(n.y) < RT_HALF) ? RTVec3(0,1,0) : RTVec3(1,0,0);
    RTVec3 T = n.cross(up).normalized();
    RTVec3 B = n.cross(T);
    rtfx s = rt_mul(bump_strength, h);
    return (n + T * s + B * s).normalized();
}

int texture_self_test() {
    int fail = 0;
    // 1. 棋盘格：(0,0) 与 (0.5,0) 应异色
    {
        Texture t = Texture::checker(RTVec3(RT_ONE,RT_ONE,RT_ONE),
                                     RTVec3(0,0,0), rt_itofx(2));
        RTVec3 c0 = t.sample(RTVec2(0,0));
        RTVec3 c1 = t.sample(RTVec2(fx::fxf(1,2),0));
        if (rt_abs(c0.x - c1.x) < fx::fxf(1,2)) fail++;
    }
    // 2. 渐变：t=0 得 c1，t=1 得 c2
    {
        Texture t = Texture::gradient(RTVec3(0,0,0), RTVec3(RT_ONE,0,0));
        RTVec3 a = t.sample(RTVec2(0,0));
        RTVec3 b = t.sample(RTVec2(RT_ONE,0));
        if (a.x != 0) fail++;
        if (!rt_near(b.x, RT_ONE, 200)) fail++;
    }
    // 3. 噪声：输出应在 [c1,c2] 之间
    {
        Texture t = Texture::noise(RTVec3(0,0,0), RTVec3(RT_ONE,RT_ONE,RT_ONE), rt_itofx(4));
        for (int i = 0; i < 20; i++) {
            rtfx u = (rtfx)(i * 3277);
            RTVec3 c = t.sample(RTVec2(u, u));
            if (c.x < 0 || c.x > RT_ONE) fail++;
        }
    }
    // 4. 凹凸：扰动后法线仍为单位长
    {
        Texture t = Texture::bump(fx::fxf(1,4));
        RTVec3 n = t.perturb_normal(RTVec3(0,RT_ONE,0), RTVec3(rt_itofx(1),rt_itofx(2),rt_itofx(3)));
        if (rt_abs(n.length() - RT_ONE) > fx::fxf(1,10)) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
