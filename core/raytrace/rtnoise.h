// ============================================================================
// nefuOS 光线追踪引擎 —— rtnoise: 程序噪声库
// ----------------------------------------------------------------------------
// 纹理/位移用：
//   - Perlin 梯度噪声（3D，确定性哈希梯度）
//   - 分形叠加 fbm3
//   - ridged multifractal（山脊状，模拟地形）
//   - turbulence（湍流，模拟云）
// 全部 Q16.16，无 STL。
// ============================================================================
#pragma once
#include "rtmath.h"

namespace nefu {
namespace raytrace {

// 3D Perlin 噪声，输出 [-1,1]
rtfx perlin3(rtfx x, rtfx y, rtfx z);
// 分形布朗运动，输出 [0,1]
rtfx fbm3(rtfx x, rtfx y, rtfx z, int octaves);
// 湍流（绝对值叠加），输出 [0,1]
rtfx turbulence(rtfx x, rtfx y, rtfx z, int octaves);
// ridged multifractal，输出 [0,1]（山脊锐利）
rtfx ridged(rtfx x, rtfx y, rtfx z, int octaves);
// 大理石纹理：由 sin(x + fbm) 产生条纹
rtfx marble(rtfx x, rtfx y, rtfx z);

int rtnoise_self_test();

} // namespace raytrace
} // namespace nefu
