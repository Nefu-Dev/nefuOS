// ============================================================================
// nefuOS 光线追踪引擎 —— primitives: 可求交几何图元
// ----------------------------------------------------------------------------
// 所有图元用 Q16.16 定点，世界空间直接摆放（变换已烘焙进顶点/中心）。
// 图元清单：
//   球体 sphere / 无限平面 plane / 三角形 triangle / 轴对齐盒 box /
//   圆环 torus / 有限圆柱 cylinder / 圆锥 cone / 圆盘 disc
// 每个图元提供：
//   - bounds()   : 轴对齐包围盒（BVH 加速用）
//   - intersect(): 射线求交，命中填 HitInfo
// 设计约定：不抛异常，未命中返回 false；self_test 返回失败条数。
// ============================================================================
#pragma once
#include "rtmath.h"

namespace nefu {
namespace raytrace {

// 图元类型标签
enum PrimType {
    PRIM_SPHERE = 0,
    PRIM_PLANE,
    PRIM_TRIANGLE,
    PRIM_BOX,
    PRIM_TORUS,
    PRIM_CYLINDER,
    PRIM_CONE,
    PRIM_DISC,
    PRIM_COUNT
};

// 二维向量（纹理坐标 / 面积光采样）
struct RTVec2 { rtfx u, v; RTVec2() : u(0), v(0) {} RTVec2(rtfx a, rtfx b) : u(a), v(b) {} };

// 求交结果记录
struct HitInfo {
    rtfx   t;          // 命中距离（-1 未命中）
    RTVec3 point;      // 命中点
    RTVec3 normal;     // 几何法线（已单位化，朝外）
    RTVec2 uv;         // 纹理坐标 [0,1]
    int    material;   // 材质索引（场景材质表下标）
    int    prim_id;    // 图元在场景中的下标
    HitInfo() : t(-1), material(0), prim_id(-1) {}
    bool hit() const { return t > 0; }
};

// ============================================================================
// Primitive ——  tagged-union 式图元（无 STL，固定字段）
// ============================================================================
struct Primitive {
    int     type;
    int     material;     // 材质索引

    RTVec3  center;      // sphere/torus/cylinder/cone/disc 中心
    rtfx    radius;      // sphere 半径 / torus 大圆 R / cylinder·cone 底半径 / disc 半径
    rtfx    radius2;     // torus 小管 r / cylinder 高 / cone 高
    rtfx    d;           // plane 偏移（n·p+d=0）
    RTVec3  normal;      // plane/disc 法线
    RTVec3  pa, pb, pc;  // triangle 三顶点
    RTVec3  bmin, bmax;  // box 两角
    RTVec3  axis;        // cylinder/cone/torus 轴向（默认 +Y）

    Primitive() : type(PRIM_SPHERE), material(0), radius(0), radius2(0), d(0),
                  normal(0, 1, 0), axis(0, 1, 0) {}

    // ---- 构造便捷函数 ----
    static Primitive make_sphere(const RTVec3& c, rtfx r, int mat);
    static Primitive make_plane(const RTVec3& n, rtfx d, int mat);       // n·p+d=0
    static Primitive make_triangle(const RTVec3& a, const RTVec3& b,
                                   const RTVec3& c, int mat);
    static Primitive make_box(const RTVec3& mn, const RTVec3& mx, int mat);
    static Primitive make_torus(const RTVec3& c, rtfx R, rtfx r, int mat);
    static Primitive make_cylinder(const RTVec3& c, rtfx r, rtfx h, int mat);
    static Primitive make_cone(const RTVec3& c, rtfx r, rtfx h, int mat);
    static Primitive make_disc(const RTVec3& c, const RTVec3& n, rtfx r, int mat);

    // 包围盒
    RTAABB bounds() const;
    // 射线求交：命中返回 true 并填 t/normal/point/uv
    bool intersect(const RTRay& ray, rtfx& t, RTVec3& nrm,
                   RTVec3& pt, RTVec2& uv) const;
};

// 便捷：整图元求交（含材质记录）
bool prim_hit(const Primitive& p, const RTRay& ray, HitInfo& h);

// self test
int primitives_self_test();

} // namespace raytrace
} // namespace nefu
