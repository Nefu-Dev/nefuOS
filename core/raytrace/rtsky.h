// ============================================================================
// nefuOS 光线追踪引擎 —— rtsky: 程序化天空模型
// ----------------------------------------------------------------------------
// Perez 风格：根据太阳方向和仰角计算天空颜色
// 简化版：水平渐变 + 太阳高光
// ============================================================================
#pragma once
#include "rtmath.h"

namespace nefu {
namespace raytrace {

struct RTSky {
    RTVec3 sun_dir;
    RTVec3 zenith;
    RTVec3 horizon;

    RTSky() {
        sun_dir = RTVec3(fx::fxf(1,2), 1, fx::fxf(1,2)).normalized();
        zenith = RTVec3(fx::fxf(2,10), fx::fxf(4,10), RT_ONE);
        horizon = RTVec3(fx::fxf(7,10), fx::fxf(8,10), RT_ONE);
    }

    // 按方向取天空色
    RTVec3 sample(const RTVec3& dir) const;
};

int rtsky_self_test();

} // namespace raytrace
} // namespace nefu
