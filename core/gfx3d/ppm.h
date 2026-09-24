// ============================================================================
// nefuOS 3D 图形库 —— ppm: PPM 图像读写（P6 二进制格式）
// ----------------------------------------------------------------------------
// PPM P6 格式：
//   "P6\n" + "宽 高\n" + "255\n" + 二进制 RGB 数据
// 用于 render3d 命令把渲染结果保存为图片，也用于 self test。
// ============================================================================
#pragma once
#include <stdint.h>

namespace nefu {
namespace gfx3d {

// 写 PPM P6 到内存缓冲，返回写入字节数（buf 由调用方分配，大小至少 15+w*h*3）
int ppm_write(const uint32_t* rgba, int w, int h, uint8_t* buf, int bufsz);

// 读 PPM P6 到新分配的 RGBA buffer（用 new[]，调用方 delete[]）。
// 失败返回 0。
uint32_t* ppm_read(const uint8_t* data, int size, int& w, int& h);

int ppm_self_test();

} // namespace gfx3d
} // namespace nefu
