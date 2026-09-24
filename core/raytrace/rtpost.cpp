// ============================================================================
// nefuOS 光线追踪引擎 —— rtpost 实现
// ============================================================================
#include "rtpost.h"

namespace nefu {
namespace raytrace {

void RTPostChain::alloc(int width, int height) {
    delete[] fb;
    w = width; h = height;
    fb = new uint32_t[w * h];
    for (int i = 0; i < w*h; i++) fb[i] = 0;
}

void RTPostChain::apply_blur() {
    if (fb) filter_blur(fb, w, h);
}

void RTPostChain::apply_sharpen(rtfx amt) {
    if (fb) filter_sharpen(fb, w, h, amt);
}

void RTPostChain::apply_dither() {
    if (fb) filter_dither(fb, w, h);
}

void RTPostChain::apply_brightness(rtfx b) {
    if (fb) filter_brightness(fb, w, h, b);
}

int rtpost_self_test() {
    int fail = 0;
    RTPostChain p;
    p.alloc(4, 4);
    // 1. 亮度不崩
    {
        p.apply_brightness(rt_itofx(2));
        if (p.fb[0] > 0xFFFFFF) fail++;
    }
    // 2. 模糊不崩
    {
        p.apply_blur();
        if (p.fb[0] > 0xFFFFFF) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
