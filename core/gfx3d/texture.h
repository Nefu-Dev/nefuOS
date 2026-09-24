// ============================================================================
// nefuOS 3D 图形库 —— texture: 纹理与图像
// ----------------------------------------------------------------------------
// 简单的 2D 纹理：RGBA8888 像素数组，支持：
//   - 生成程序纹理（棋盘格、渐变、噪声）
//   - 最近邻 / 双线性采样
//   - 翻转 / 旋转
//   - PPM 加载/保存
// ============================================================================
#pragma once
#include "math3d.h"
#include "raster.h"

namespace nefu {
namespace gfx3d {

struct Texture {
    int w, h;
    uint32_t* pixels;   // RGBA8888, row-major

    Texture() : w(0), h(0), pixels(0) {}
    void alloc(int W, int H);
    void free();
    void fill(uint32_t c);

    // 生成程序纹理
    void make_checkerboard(int size, uint32_t c0, uint32_t c1);
    void make_gradient(uint32_t top, uint32_t bottom);
    void make_noise();

    // 采样
    uint32_t sample_nearest(double u, double v) const;
    uint32_t sample_bilinear(double u, double v) const;
};

int texture_self_test();

} // namespace gfx3d
} // namespace nefu
