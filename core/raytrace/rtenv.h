// ============================================================================
// nefuOS 光线追踪引擎 —— rtenv: 环境光 / 程序化 HDRI
// ----------------------------------------------------------------------------
// 用方向函数模拟环境贴图：
//   - 渐变天空（地平线到天顶）
//   - 程序化太阳（高光圆盘）
//   - 星云（噪声驱动）
//   - 全局环境光 PDF（用于 NEE）
// 路径追踪采样环境光时调用 env_lookup(dir)。
// ============================================================================
#pragma once
#include "rtmath.h"

namespace nefu {
namespace raytrace {

struct RTEnv {
    RTVec3 top;       // 天顶色
    RTVec3 horizon;  // 地平线色
    RTVec3 bottom;    // 地面色
    RTVec3 sun_dir;   // 太阳方向（单位）
    RTVec3 sun_color; // 太阳颜色
    rtfx   sun_size;  // 太阳角半径（cos）
    rtfx   intensity;

    RTEnv() : intensity(RT_ONE) {
        top = RTVec3(fx::fxf(2,10),fx::fxf(4,10),RT_ONE);
        horizon = RTVec3(fx::fxf(8,10),fx::fxf(8,10),fx::fxf(9,10));
        bottom = RTVec3(fx::fxf(2,10),fx::fxf(2,10),fx::fxf(2,10));
        sun_dir = RTVec3(0, RT_ONE, 0).normalized();
        sun_color = RTVec3(RT_ONE, rt_itofx(9), rt_itofx(6));
        sun_size = fx::fxf(99,100);
    }

    // 按方向查环境光颜色
    RTVec3 lookup(const RTVec3& dir) const;
    // 环境光 PDF（方向采样）
    rtfx pdf(const RTVec3& dir) const;
    // 随机采样一个光源方向
    RTVec3 sample(RTRng& rng) const;
};

int rtenv_self_test();

} // namespace raytrace
} // namespace nefu
