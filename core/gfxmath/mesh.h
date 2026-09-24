// nefuOS gfxmath —— 网格 mesh
// 教学版：三角形网格（顶点 + 法线 + 索引），支持生成基本体与法线计算。
// 用于 3D 渲染的几何数据容器。class / STL / 中文注释。
#pragma once
#include <vector>
#include "gfxmath/vec3.h"
#include "gfxmath/mat4.h"

namespace nefu {
namespace gfx {

// 网格顶点
struct Vertex {
    Vec3 pos;    // 位置
    Vec3 normal; // 法线
    Vertex() : pos(), normal() {}
    Vertex(const Vec3& p) : pos(p), normal() {}
};

// 三角形网格
class Mesh {
public:
    Mesh();

    // 清空
    void clear();
    // 添加顶点（返回索引）
    int add_vertex(const Vec3& p);
    // 添加三角形（三个顶点索引，返回三角形序号）
    int add_triangle(int a, int b, int c);
    // 顶点数 / 三角形数
    int vertex_count() const { return (int)verts.size(); }
    int triangle_count() const { return (int)tris.size() / 3; }

    // 顶点 / 三角形访问
    Vertex vertex(int i) const { return verts[i]; }
    void get_triangle(int i, int& a, int& b, int& c) const;

    // 计算所有顶点的面法线平均（光照用）
    void compute_normals();

    // 顶点/法线数据指针（渲染方便）
    const float* pos_data() const { return (const float*)verts.data(); }
    int pos_floats() const { return (int)verts.size() * 3; }

    // 生成基本体
    static Mesh cube(float size);                 // 立方体
    static Mesh sphere(float radius, int stacks, int slices);   // 球体
    static Mesh plane(float w, float h, int gw, int gh);        // 网格平面

    // 变换网格（顶点位置与法线）
    void transform(const Mat4& m, const Mat4& normal_m);

    // 轴对齐包围盒
    void bounding_box(Vec3& minb, Vec3& maxb) const;

    // ---- self test ----
    static int self_test();

private:
    std::vector<Vertex> verts;
    std::vector<int> tris;   // 每 3 个一组
};

} // namespace gfx
} // namespace nefu
