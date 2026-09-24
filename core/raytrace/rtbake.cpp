// ============================================================================
// nefuOS 光线追踪引擎 —— rtbake 实现（Q16.16 定点）
// ============================================================================
#include "rtbake.h"

namespace nefu {
namespace raytrace {

void RTBake::alloc(int width, int height) {
    delete[] luma; delete[] color;
    w = width; h = height;
    luma = new rtfx[w * h];
    color = new RTVec3[w * h];
    clear();
}

void RTBake::clear() {
    if (!luma || !color) return;
    for (int i = 0; i < w * h; i++) { luma[i] = 0; color[i] = RTVec3(0,0,0); }
}

void RTBake::bake_constant(const RTVec3& c) {
    for (int i = 0; i < w * h; i++) {
        color[i] = c;
        luma[i] = rt_mul(c.x, fx::fxf(3,10)) + rt_mul(c.y, fx::fxf(6,10)) + rt_mul(c.z, fx::fxf(1,10));
    }
}

void RTBake::bake_gradient(const RTVec3& top, const RTVec3& bot) {
    for (int y = 0; y < h; y++) {
        rtfx t = rt_div(rt_itofx(y), rt_itofx(h));
        RTVec3 c = rt_lerp(bot, top, t);
        for (int x = 0; x < w; x++) {
            int i = y * w + x;
            color[i] = c;
            luma[i] = rt_mul(c.x, fx::fxf(3,10)) + rt_mul(c.y, fx::fxf(6,10)) + rt_mul(c.z, fx::fxf(1,10));
        }
    }
}

RTVec3 RTBake::sample(int x, int y) const {
    if (!color) return RTVec3(0,0,0);
    if (x < 0) x = 0; if (x >= w) x = w - 1;
    if (y < 0) y = 0; if (y >= h) y = h - 1;
    return color[y * w + x];
}

rtfx RTBake::avg_luma() const {
    if (!luma || w * h <= 0) return 0;
    rtfx sum = 0;
    for (int i = 0; i < w * h; i++) sum += luma[i];
    return rt_div(sum, rt_itofx(w * h));
}

int rtbake_self_test() {
    int fail = 0;
    RTBake b;
    b.alloc(8, 8);
    // 1. 常量烘焙
    {
        b.bake_constant(RTVec3(RT_ONE, RT_ONE, RT_ONE));
        RTVec3 c = b.sample(4, 4);
        if (!rt_near(c.x, RT_ONE, 300)) fail++;
    }
    // 2. 渐变：顶到底不同
    {
        b.bake_gradient(RTVec3(RT_ONE,0,0), RTVec3(0,0,RT_ONE));
        RTVec3 top = b.sample(4, 7);
        RTVec3 bot = b.sample(4, 0);
        if (rt_near(top.x, bot.x, fx::fxf(2,10))) fail++;
    }
    // 3. 平均亮度
    {
        rtfx l = b.avg_luma();
        if (l <= 0) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
