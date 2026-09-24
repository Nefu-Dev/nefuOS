// ============================================================================
// nefuOS 光线追踪引擎 —— rtworld: 世界根节点
// ----------------------------------------------------------------------------
// 聚合：场景 + 渲染器 + 相机路径 + 累积器
// 提供一帧渲染的完整入口。
// ============================================================================
#pragma once
#include "rtmath.h"
#include "scene.h"
#include "rtrender.h"
#include "rtcampath.h"

namespace nefu {
namespace raytrace {

struct RTWorld {
    Scene     scene;
    RTRenderer renderer;
    RTCamPath  path;
    int        frame;

    RTWorld() : frame(0) {}

    void load(int preset);
    void tick();
    void render();
};

int rtworld_self_test();

} // namespace raytrace
} // namespace nefu
