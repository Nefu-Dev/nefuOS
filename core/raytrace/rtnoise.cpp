// ============================================================================
// nefuOS 光线追踪引擎 —— rtnoise 实现（Q16.16 定点，Perlin）
// ============================================================================
#include "rtnoise.h"

namespace nefu {
namespace raytrace {

// 哈希梯度：返回 3D 梯度方向的伪随机分量
static void hash_grad(int xi, int yi, int zi, rtfx& gx, rtfx& gy, rtfx& gz) {
    uint32_t h = (uint32_t)(xi * 73856093 ^ yi * 19349663 ^ zi * 83492791);
    h = (h ^ (h >> 15)) * 1103515245u;
    h = h ^ (h >> 13);
    // 把 hash 映射到 6 个方向之一
    int axis = (h >> 2) & 7;
    gx = gy = gz = 0;
    switch (axis) {
    case 0: gx = RT_ONE; break;
    case 1: gx = -RT_ONE; break;
    case 2: gy = RT_ONE; break;
    case 3: gy = -RT_ONE; break;
    case 4: gz = RT_ONE; break;
    default: gz = -RT_ONE; break;
    }
}

static rtfx fade(rtfx t) {
    // 6t^5 - 15t^4 + 10t^3
    rtfx t2 = rt_mul(t, t);
    rtfx t3 = rt_mul(t2, t);
    return rt_mul(t3, rt_itofx(10) - rt_mul(rt_itofx(15), t) + rt_mul(rt_itofx(6), t2));
}

rtfx perlin3(rtfx x, rtfx y, rtfx z) {
    int xi = x >> 16, yi = y >> 16, zi = z >> 16;
    rtfx xf = x & 0xFFFF, yf = y & 0xFFFF, zf = z & 0xFFFF;
    rtfx u = fade(xf), v = fade(yf), w = fade(zf);
    rtfx grad[8][3];
    for (int i = 0; i < 8; i++) {
        hash_grad(xi + (i & 1), yi + ((i >> 1) & 1), zi + ((i >> 2) & 1),
                  grad[i][0], grad[i][1], grad[i][2]);
    }
    auto dot = [&](int i, rtfx dx, rtfx dy, rtfx dz) -> rtfx {
        return rt_mul(grad[i][0], dx) + rt_mul(grad[i][1], dy) + rt_mul(grad[i][2], dz);
    };
    rtfx x00 = dot(0, xf, yf, zf)     + rt_mul(u, dot(1, xf-RT_ONE, yf, zf) - dot(0, xf, yf, zf));
    rtfx x10 = dot(2, xf, yf-RT_ONE, zf) + rt_mul(u, dot(3, xf-RT_ONE, yf-RT_ONE, zf) - dot(2, xf, yf-RT_ONE, zf));
    rtfx x01 = dot(4, xf, yf, zf-RT_ONE) + rt_mul(u, dot(5, xf-RT_ONE, yf, zf-RT_ONE) - dot(4, xf, yf, zf-RT_ONE));
    rtfx x11 = dot(6, xf, yf-RT_ONE, zf-RT_ONE) + rt_mul(u, dot(7, xf-RT_ONE, yf-RT_ONE, zf-RT_ONE) - dot(6, xf, yf-RT_ONE, zf-RT_ONE));
    rtfx y0 = x00 + rt_mul(v, x10 - x00);
    rtfx y1 = x01 + rt_mul(v, x11 - x01);
    return y0 + rt_mul(w, y1 - y0);   // [-1,1]
}

rtfx fbm3(rtfx x, rtfx y, rtfx z, int octaves) {
    rtfx sum = 0, amp = RT_ONE, freq = RT_ONE, norm = 0;
    for (int i = 0; i < octaves; i++) {
        sum += rt_mul(perlin3(rt_mul(x,freq), rt_mul(y,freq), rt_mul(z,freq)), amp);
        norm += amp;
        amp = rt_mul(amp, RT_HALF);
        freq = rt_mul(freq, rt_itofx(2));
    }
    return rt_div(sum + RT_ONE, rt_itofx(2));   // [-1,1] -> [0,1]
}

rtfx turbulence(rtfx x, rtfx y, rtfx z, int octaves) {
    rtfx sum = 0, amp = RT_ONE, freq = RT_ONE, norm = 0;
    for (int i = 0; i < octaves; i++) {
        rtfx n = perlin3(rt_mul(x,freq), rt_mul(y,freq), rt_mul(z,freq));
        if (n < 0) n = -n;
        sum += rt_mul(n, amp);
        norm += amp;
        amp = rt_mul(amp, RT_HALF);
        freq = rt_mul(freq, rt_itofx(2));
    }
    return rt_div(sum, norm);
}

rtfx ridged(rtfx x, rtfx y, rtfx z, int octaves) {
    rtfx sum = 0, amp = RT_ONE, freq = RT_ONE, norm = 0;
    for (int i = 0; i < octaves; i++) {
        rtfx n = perlin3(rt_mul(x,freq), rt_mul(y,freq), rt_mul(z,freq));
        n = RT_ONE - rt_abs(n);   // 山脊
        sum += rt_mul(rt_mul(n,n), amp);
        norm += amp;
        amp = rt_mul(amp, RT_HALF);
        freq = rt_mul(freq, rt_itofx(2));
    }
    return rt_div(sum, norm);
}

rtfx marble(rtfx x, rtfx y, rtfx z) {
    rtfx n = fbm3(x, y, z, 4);
    rtfx v = rt_mul(fx::fx_sin(rt_mul(x, rt_itofx(3)) + rt_mul(n, RT_2PI)), RT_HALF) + RT_HALF;
    return v;
}

int rtnoise_self_test() {
    int fail = 0;
    // 1. perlin 在 [~-1,1]
    {
        for (int i = 0; i < 20; i++) {
            rtfx v = perlin3(rt_itofx(i), rt_itofx(i*2), rt_itofx(i*3));
            if (v < -RT_ONE - 100 || v > RT_ONE + 100) fail++;
        }
    }
    // 2. fbm3 在 [0,1]
    {
        for (int i = 0; i < 20; i++) {
            rtfx v = fbm3(rt_itofx(i), rt_itofx(i), rt_itofx(i), 4);
            if (v < 0 || v > RT_ONE + 100) fail++;
        }
    }
    // 3. turbulence 在 [0,1]
    {
        rtfx v = turbulence(rt_itofx(2), rt_itofx(2), rt_itofx(2), 4);
        if (v < 0 || v > RT_ONE + 100) fail++;
    }
    // 4. marble 在 [0,1]
    {
        rtfx v = marble(rt_itofx(1), rt_itofx(1), rt_itofx(1));
        if (v < 0 || v > RT_ONE + 100) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
