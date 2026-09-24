// ============================================================================
// nefuOS 光线追踪引擎 —— rtsampler 实现（Q16.16 定点）
// ============================================================================
#include "rtsampler.h"

namespace nefu {
namespace raytrace {

void RTSampler::reset(int samples) {
    spp = samples;
    cur = 0;
}

void RTSampler::sample_stratified(rtfx& u, rtfx& v) {
    int n = 1;
    while (n * n < spp) n++;
    int i = cur % n;
    int j = (cur / n) % n;
    u = rt_div(rt_itofx(i) + rng.next_fx(), rt_itofx(n));
    v = rt_div(rt_itofx(j) + rng.next_fx(), rt_itofx(n));
    cur++;
}

// Halton：可逆基数展开
static rtfx halton_digit(int index, int base) {
    rtfx f = RT_ONE;
    rtfx r = 0;
    int i = index;
    while (i > 0) {
        f = rt_div(f, rt_itofx(base));
        r += rt_mul(f, rt_itofx(i % base));
        i = i / base;
    }
    return r;
}

void RTSampler::sample_halton(int i, rtfx& u, rtfx& v) {
    u = halton_digit(i + 1, 2);
    v = halton_digit(i + 1, 3);
}

void RTSampler::sample_disk(rtfx& x, rtfx& y) {
    rtfx a = rng.next_fx();
    rtfx b = rng.next_fx();
    // 同心映射：把 [0,1)^2 映到单位圆
    rtfx r = rt_sqrt(b);
    rtfx theta = rt_mul(a, RT_2PI);
    x = rt_mul(r, fx::fx_cos(theta));
    y = rt_mul(r, fx::fx_sin(theta));
}

RTVec3 RTSampler::sample_cosine_hemisphere(const RTVec3& normal) {
    rtfx u1 = rng.next_fx();
    rtfx u2 = rng.next_fx();
    rtfx r = rt_sqrt(u1);
    rtfx phi = rt_mul(u2, RT_2PI);
    rtfx x = rt_mul(r, fx::fx_cos(phi));
    rtfx y = rt_mul(r, fx::fx_sin(phi));
    rtfx z = rt_sqrt(RT_ONE - u1);
    // 局部坐标系
    RTVec3 up = (rt_abs(normal.y) < RT_HALF) ? RTVec3(0,1,0) : RTVec3(1,0,0);
    RTVec3 T = normal.cross(up).normalized();
    RTVec3 B = normal.cross(T);
    return T * x + B * y + normal * z;
}

int rtsampler_self_test() {
    int fail = 0;
    // 1. 分层采样都在 [0,1)
    {
        RTSampler s(42);
        s.reset(16);
        for (int i = 0; i < 16; i++) {
            rtfx u, v;
            s.sample_stratified(u, v);
            if (u < 0 || u >= RT_ONE || v < 0 || v >= RT_ONE) fail++;
        }
    }
    // 2. Halton 在 [0,1)
    {
        RTSampler s(1);
        for (int i = 0; i < 20; i++) {
            rtfx u, v;
            s.sample_halton(i, u, v);
            if (u < 0 || u >= RT_ONE || v < 0 || v >= RT_ONE) fail++;
        }
    }
    // 3. 圆盘采样：半径 <= 1
    {
        RTSampler s(7);
        for (int i = 0; i < 20; i++) {
            rtfx x, y;
            s.sample_disk(x, y);
            rtfx r2 = rt_mul(x, x) + rt_mul(y, y);
            if (r2 > RT_ONE + 100) fail++;
        }
    }
    // 4. 半球采样方向与法线同侧
    {
        RTSampler s(99);
        RTVec3 n(0, RT_ONE, 0);
        RTVec3 d = s.sample_cosine_hemisphere(n);
        if (d.y <= 0) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
