// ============================================================================
// nefuOS 光线追踪引擎 —— rtmesh: 索引三角形网格
// ----------------------------------------------------------------------------
// 把一批三角形打包成网格，配合 BVH 使用：
//   - 顶点数组 + 索引数组（索引三元组=一个三角形）
//   - 逐顶点法线（平滑着色）
//   - intersect() 遍历网格内三角形，返回命中与重心坐标
// 顶点数据用 new[] 分配，析构释放。
// ============================================================================
#pragma once
#include "rtmath.h"
#include "primitives.h"

namespace nefu {
namespace raytrace {

struct RTMesh {
    RTVec3* verts;       // 顶点数组
    RTVec3* normals;     // 顶点法线（可空）
    int*    indices;     // 索引数组，每 3 个 = 一个三角形
    int     n_verts;
    int     n_tris;      // 三角形数 = n_idx/3
    RTAABB  box;

    RTMesh() : verts(0), normals(0), indices(0), n_verts(0), n_tris(0) {}
    ~RTMesh() { delete[] verts; delete[] normals; delete[] indices; }

    // 分配 n 个顶点、tri_count 个三角形
    void alloc(int n, int tri_count);

    // 网格求交：返回最近命中距离 t，填重心/法线
    bool intersect(const RTRay& ray, rtfx& t, RTVec3& normal, RTVec3& point,
                   int& out_tri) const;

    // 重建包围盒
    void compute_bounds();
};

// 生成立方体网格（中心 c，半边长 e）
RTMesh* rt_make_cube_mesh(const RTVec3& c, rtfx e);
// 生成 UV 球体网格（中心 c，半径 r，经度/纬度分段）
RTMesh* rt_make_sphere_mesh(const RTVec3& c, rtfx r, int seg_u, int seg_v);

int rtmesh_self_test();

} // namespace raytrace
} // namespace nefu
