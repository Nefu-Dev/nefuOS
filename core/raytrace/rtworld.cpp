// ============================================================================
// nefuOS 光线追踪引擎 —— rtworld 实现
// ============================================================================
#include "rtworld.h"

namespace nefu {
namespace raytrace {

void RTWorld::load(int preset) {
    scene.load_preset(preset);
    renderer.setup(16, 16);
    frame = 0;
}

void RTWorld::tick() {
    frame++;
}

void RTWorld::render() {
    renderer.render_pass(scene);
}

int rtworld_self_test() {
    int fail = 0;
    RTWorld w;
    w.load(SCENE_CORNELL);
    // 1. 渲染一帧
    {
        w.render();
        if (w.renderer.current_pass != 1) fail++;
    }
    // 2. tick
    {
        w.tick();
        if (w.frame != 1) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
