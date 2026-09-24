// ============================================================================
// nefuOS 3D 图形库 —— raster: 软件光栅化器
// ----------------------------------------------------------------------------
// 完整的 CPU 渲染管线（不依赖 GPU）：
//   1. 输入：已经过 model*view*projection 变换的裁剪空间顶点
//   2. 视锥裁剪：Sutherland-Hodgman 对 6 个裁剪面裁剪
//   3. 透视除法 + 视口变换 -> 屏幕坐标
//   4. 背面剔除：屏幕空间绕序判断
//   5. 光栅化：包围盒遍历 + 重心坐标测试
//   6. 透视校正插值：属性 / w 与 1/w 线性插值后再相除
//   7. 深度测试：z-buffer（w-buffer 可选）
//   8. 片元回调：由外部（light 模块 / 应用）计算颜色
//
// 另含：线段裁剪 Liang-Barsky、Bresenham 直线、点/线/面绘制、alpha 混合、
//       最近邻 / 双线性纹理采样。
// ============================================================================
#pragma once
#include <stdint.h>
#include "math3d.h"
#include "../gfxlib/raster.h"

namespace nefu {
namespace gfx3d {

// 光栅化器传给片元回调的插值结果
struct FragInput {
    Vec3 world;       // 世界坐标（已透视校正）
    Vec3 normal;      // 世界法线（已归一由调用方处理）
    Vec2 uv;
    Vec4 color;       // 逐顶点颜色插值
    float depth;      // 屏幕深度 [0,1]
};

// 片元回调：根据插值结果输出一个 0xAARRGGBB 颜色。
// user 是光栅化器上挂载的任意指针（常放材质/光照参数）。
typedef void (*FragmentShader)(const FragInput& in, void* user, uint32_t& out_color);

// 光栅化器状态
struct Rasterizer {
    gfxlib::Buffer target;       // 离屏像素目标
    float* zbuf;                 // 深度缓冲（每像素一个 float）
    int w, h;
    double viewport_x, viewport_y;   // 视口原点（像素）
    double viewport_w, viewport_h;   // 视口尺寸

    bool backface_cull;          // 背面剔除开关
    bool alpha_test;             // alpha 测试
    FragmentShader frag_shader;  // 片元着色回调
    void* frag_user;

    Rasterizer() : zbuf(0), w(0), h(0),
                   viewport_x(0), viewport_y(0), viewport_w(0), viewport_h(0),
                   backface_cull(true), alpha_test(false),
                   frag_shader(0), frag_user(0) {}

    // 绑定离屏 buffer，分配深度缓冲
    void attach(gfxlib::Buffer buf);
    void detach();

    void clear(uint32_t color);
    void clear_depth();

    // 裁剪空间顶点（clip 坐标 w 可能为负）
    struct ClipVert {
        Vec4 clip;
        Vec3 world;
        Vec3 normal;
        Vec2 uv;
        Vec4 color;
    };

    // 输入三个裁剪空间顶点，完成裁剪+光栅化
    void draw_triangle(const ClipVert& v0, const ClipVert& v1, const ClipVert& v2);

    // 直接画屏幕空间线段（供线框模式使用）
    void draw_line_screen(int x0, int y0, int x1, int y1, uint32_t color);
    void draw_point(int x, int y, uint32_t color);
};

// ============================================================================
// 独立工具函数
// ============================================================================
// Liang-Barsky 线段裁剪：把 (x0,y0)-(x1,y1) 裁剪到 [xmin,ymin]-[xmax,ymax]
// 返回 false 表示完全在窗外
bool clip_line_liang_barsky(int& x0, int& y0, int& x1, int& y1,
                            int xmin, int ymin, int xmax, int ymax);

// 纹理采样： nearest / bilinear。tex 为 w*h 的 0xAARRGGBB 数据
uint32_t sample_texture_nearest(const uint32_t* tex, int tw, int th, double u, double v);
uint32_t sample_texture_bilinear(const uint32_t* tex, int tw, int th, double u, double v);

// alpha 混合：src 覆盖到 dst 上（src 自带 alpha）
uint32_t alpha_blend(uint32_t src, uint32_t dst);

// 打包/解包颜色
uint32_t pack_color(double r, double g, double b, double a = 1.0);
void unpack_color(uint32_t c, double& r, double& g, double& b);

// ---- mipmap 生成：从一张图生成金字塔（每级 1/2 尺寸，平均采样）----
// levels 输出每个 mip 的指针（已 new[]，调用方 delete[] 数组元素）
void mipmap_generate(const uint32_t* src, int w, int h,
                     uint32_t** levels_out, int* lw_out, int* lh_out, int max_levels);

// 三线性采样：根据 mip 层级 t 在相邻两层间双线性插值
uint32_t sample_texture_trilinear(const uint32_t* const* levels,
                                  const int* lw, const int* lh, int levels_count,
                                  double u, double v, double lod);

// 扫描线光栅化（备用实现）：对一个屏幕空间三角形做扫描线填充，
// 每个像素调用一次 fragment shader。供教学对比 bbox 方法。
void raster_scanline(Rasterizer& r,
                     double x0, double y0, double x1, double y1, double x2, double y2);

// self test
int raster_self_test();

} // namespace gfx3d
} // namespace nefu
