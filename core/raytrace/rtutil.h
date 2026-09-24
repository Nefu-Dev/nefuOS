// ============================================================================
// nefuOS 光线追踪引擎 —— rtutil: 帧缓冲与图像工具
// ----------------------------------------------------------------------------
// 把 PathTracer 输出的 Q16.16 颜色转成可显示像素：
//   - tonemap + gamma + RGB888
//   - 最近邻放大 blit
//   - PPM 文本输出（host 端调试保存用）
//   - 亮度/对比度调整
// 无 FPU、无 STL，纯定点。
// ============================================================================
#pragma once
#include "rtmath.h"
#include "../klib/klib.h"

namespace nefu {
namespace raytrace {

// 把浮点颜色像素写入 RGB888 帧缓冲（Reinhard + gamma）
void rt_write_pixel(uint32_t* fb, int x, int y, int w, int h, const RTVec3& color);

// 把小 fb 放大到 surface（最近邻），scale 为整数倍
void rt_blit_upscale(const uint32_t* src, int sw, int sh,
                     uint32_t* dst, int dw, int dh, int scale);

// 亮度调整：bright in [0.5, 2]
RTVec3 rt_adjust_brightness(const RTVec3& c, rtfx bright);

// 对比度：mid=0.5
RTVec3 rt_adjust_contrast(const RTVec3& c, rtfx contrast);

// 写出 PPM P3 文本（host 端调试），返回写入字节数
int rt_write_ppm(const char* path, const uint32_t* fb, int w, int h);

// 计算帧缓冲平均亮度（诊断用）
rtfx rt_frame_avg_luma(const uint32_t* fb, int w, int h);

int rtutil_self_test();

} // namespace raytrace
} // namespace nefu
