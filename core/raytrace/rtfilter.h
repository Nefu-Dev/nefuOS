// ============================================================================
// nefuOS 光线追踪引擎 —— rtfilter: 渲染后处理滤镜
// ----------------------------------------------------------------------------
// 对最终帧缓冲做：
//   - 盒式模糊（去噪）
//   - 锐化
//   - 抖动（dithering，减少色带）
//   - 对比度/亮度曲线
// 原地操作 uint32_t RGB888 帧缓冲。
// ============================================================================
#pragma once
#include "rtmath.h"

namespace nefu {
namespace raytrace {

// 盒式模糊（3x3）
void filter_blur(uint32_t* fb, int w, int h);
// 锐化（拉普拉斯）
void filter_sharpen(uint32_t* fb, int w, int h, rtfx amount);
// 有序抖动（Bayer 4x4），减少色带
void filter_dither(uint32_t* fb, int w, int h);
// 亮度调整
void filter_brightness(uint32_t* fb, int w, int h, rtfx bright);
// 提取亮度到浮点缓冲
void filter_extract_luma(const uint32_t* fb, int w, int h, rtfx* out);
// 反交错（隔行变逐行）
void filter_deinterlace(uint32_t* fb, int w, int h);

int rtfilter_self_test();

} // namespace raytrace
} // namespace nefu
