// ============================================================================
// nefuOS 光线追踪引擎 —— rtonemap: 色调映射与色彩转换
// ----------------------------------------------------------------------------
// 路径追踪输出的 radiance 可能远超 [0,1]，需映射到显示器可显示范围：
//   - Reinhard:  c/(1+c)
//   - ACES 近似（Narkowicz）
//   - 电影级 filmic（Hable 近似，分段）
//   - sRGB gamma 编码
// 全部 Q16.16 定点近似。
// ============================================================================
#pragma once
#include "rtmath.h"

namespace nefu {
namespace raytrace {

// Reinhard 全局色调映射
RTVec3 tonemap_reinhard(const RTVec3& c);
// ACES 近似（Narkowicz）
RTVec3 tonemap_aces(const RTVec3& c);
// Filmic（简化分段，暗部保留高光压缩）
RTVec3 tonemap_filmic(const RTVec3& c);
// 曝光调整：exposure in [0.5, 8]
RTVec3 color_exposure(const RTVec3& c, rtfx exposure);
// sRGB gamma 编码（近似 1/2.2）
RTVec3 color_srgb_encode(const RTVec3& c);
// 转灰度（luminance）
rtfx color_luma(const RTVec3& c);
// 色域 clamp 到 [0,1]
RTVec3 color_saturate(const RTVec3& c);

int rtonemap_self_test();

} // namespace raytrace
} // namespace nefu
