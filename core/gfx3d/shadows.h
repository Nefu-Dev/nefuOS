// ============================================================================
// nefuOS 3D 图形库 —— shadows: 实时阴影
// ----------------------------------------------------------------------------
// 实现两种经典实时阴影技术：
//   1. 投影阴影（静态）：把物体沿光源方向压到地面平面，画暗色版本
//   2. 阴影体（Shadow Volume）：把背面沿光源方向挤出，形成锥体
//   3. 阴影贴图近似：从光源视角渲染深度图，片元比较深度
// ============================================================================
#pragma once
#include "math3d.h"
#include "mesh.h"
#include "raster.h"

namespace nefu {
namespace gfx3d {

// 投影阴影：生成一个"压扁"的网格副本（沿光源方向投影到地面）
// ground_y 地面高度，light_dir 指向光源方向
Mesh project_shadow_mesh(const Mesh& src, const Vec3& light_dir, double ground_y);

// 阴影体：从网格生成一个闭合锥体（挤出背面到无穷远）
// 简化版：只生成挤出侧面三角形，供 stencil 算法近似
Mesh build_shadow_volume(const Mesh& src, const Vec3& light_dir, double extrude);

// 阴影贴图：从光源视角渲染一张深度图（简化版：用 float 数组）
struct ShadowMap {
    int w, h;
    float* depth;
    Mat4 light_vp;
    ShadowMap() : w(0), h(0), depth(0) {}
    void alloc(int W, int H);
    void free();
    void clear();
    // 查询某点在光源空间是否被遮挡
    bool is_occluded(const Vec3& world_p) const;
};

// 从光源方向渲染场景深度到 ShadowMap（简化版：只传一个网格）
void render_shadow_map(ShadowMap& sm, const Mesh& m, const Mat4& model,
                       const Vec3& light_pos, double ortho_size);

int shadows_self_test();

} // namespace gfx3d
} // namespace nefu
