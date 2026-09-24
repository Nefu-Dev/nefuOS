// ============================================================================
// nefuOS 光线追踪引擎 —— rtrender 实现
// ============================================================================
#include "rtrender.h"

namespace nefu {
namespace raytrace {

void RTRenderer::setup(int w, int h) {
    accum.alloc(w, h);
    current_pass = 0;
}

rtfx RTRenderer::render_pass(const Scene& scene) {
    int w = accum.w, h = accum.h;
    if (w <= 0 || h <= 0) return 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            rtfx u = rt_div(rt_itofx(x), rt_itofx(w));
            rtfx v = rt_div(rt_itofx(y), rt_itofx(h));
            RTRng rng;
            RTRay ray = scene.cam.generate_ray(u, v, rng);
            RTVec3 c = pt.trace(ray, 0, rng, scene.bvh,
                                const_cast<Scene&>(scene).mats.data(), scene.mats.size(),
                                const_cast<Scene&>(scene).lights.data(), scene.lights.size());
            accum.add(x, y, c);
        }
    }
    current_pass++;
    return rt_div(rt_itofx(current_pass), rt_itofx(spp));
}

uint32_t RTRenderer::pixel(int x, int y) const {
    RTVec3 c = accum.avg(x, y);
    return rt_color_to_rgb888(c);
}

int rtrender_self_test() {
    int fail = 0;
    RTRenderer r;
    r.setup(4, 4);
    Scene s;
    s.load_preset(SCENE_CORNELL);
    // 1. 渲染一帧不崩
    {
        rtfx prog = r.render_pass(s);
        if (prog < 0) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
