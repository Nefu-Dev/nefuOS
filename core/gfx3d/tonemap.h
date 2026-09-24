// ============================================================================
// nefuOS 3D 图形库 —— tonemap: 色调映射与后处理
// ----------------------------------------------------------------------------
// HDR 颜色 -> LDR 显示颜色的映射：
//   - Reinhard 全局色调映射
//   - ACES 近似（电影感）
//   - 曝光 / 伽马校正
//   - 简单 bloom（模糊叠加近似）
// ============================================================================
#pragma once
#include "math3d.h"

namespace nefu {
namespace gfx3d {

// Reinhard: c / (1 + c)
Vec3 tonemap_reinhard(const Vec3& c, double exposure = 1.0);
// ACES 近似（Narkowicz）
Vec3 tonemap_aces(const Vec3& c, double exposure = 1.0);
// 线性曝光
Vec3 tonemap_exposure(const Vec3& c, double exposure);
// 伽马校正
Vec3 gamma_correct(const Vec3& c, double gamma = 2.2);

// 后处理：对整张 buffer 应用色调映射（原地修改）
void tonemap_buffer(uint32_t* buf, int w, int h, double exposure, bool aces);

// 简单 bloom：高亮区域模糊叠加（近似版：只做高亮提取+叠加）
void bloom_approx(uint32_t* buf, int w, int h, double threshold, double intensity);

int tonemap_self_test();

} // namespace gfx3d
} // namespace nefu
