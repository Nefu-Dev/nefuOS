// ============================================================================
// nefuOS 光线追踪引擎 —— rtmesh2: 程序化网格扩展
// ----------------------------------------------------------------------------
// 生成：
//   - 平面网格（地面）
//   - 球体细分网格
//   - 圆柱体网格
// 输出三角形列表。
// ============================================================================
#pragma once
#include "rtmath.h"
#include "primitives.h"

namespace nefu {
namespace raytrace {

struct RTProceduralMesh {
    Primitive* tris;
    int count;
    int cap;
    RTProceduralMesh() : tris(0), count(0), cap(0) {}
    ~RTProceduralMesh() { delete[] tris; }

    void alloc(int n);
    void push_tri(const RTVec3& a, const RTVec3& b, const RTVec3& c, int mat);
    // 生成 n x n 平面（z=0）
    void gen_plane(int n, rtfx size, int mat);
    // 生成 UV 球
    void gen_uv_sphere(int rings, int segs, const RTVec3& c, rtfx r, int mat);
};

int rtmesh2_self_test();

} // namespace raytrace
} // namespace nefu
