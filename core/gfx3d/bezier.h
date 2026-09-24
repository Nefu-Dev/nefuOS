// ============================================================================
// nefuOS 3D 图形库 —— bezier: Bezier 曲面细分
// ----------------------------------------------------------------------------
// 实现 4x4 Bezier 曲面片细分（de Casteljau 或直接 Bernstein 基函数），
// 并内置 Utah 茶壶的一组曲面片控制点（简化版 10 片），生成经典茶壶。
// ============================================================================
#pragma once
#include "math3d.h"
#include "mesh.h"

namespace nefu {
namespace gfx3d {

// 用 Bernstein 基函数在一个 4x4 控制点网格上求值，返回 (u,v) 处的位置和法线
Vec3 bezier_patch_point(const Vec3 cp[16], double u, double v);
Vec3 bezier_patch_normal(const Vec3 cp[16], double u, double v);

// 细分一个曲面片到 out 网格，subdiv 为每边细分数
void bezier_patch_mesh(const Vec3 cp[16], int subdiv, Mesh& out);

// Utah 茶壶（真实控制点版）：生成完整茶壶
void make_utah_teapot(Mesh& out, double scale = 1.0, int subdiv = 3);

int bezier_self_test();

} // namespace gfx3d
} // namespace nefu
