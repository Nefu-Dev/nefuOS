// nefuOS gfxmath —— 射线 ray 与平面 plane
// 教学版：射线（起点+方向）与平面（点+法线）的求交，用于光线追踪与拾取。
// 两个小类放同一个头，均为纯几何教学实现。
#pragma once
#include <cmath>
#include "gfxmath/vec3.h"

namespace nefu {
namespace gfx {

// 射线
struct Ray {
    Vec3 origin;   // 起点
    Vec3 dir;      // 方向（不必为单位向量）

    Ray() : origin(), dir(1, 0, 0) {}
    Ray(const Vec3& o, const Vec3& d) : origin(o), dir(d) {}

    // 参数位置 P(t) = origin + dir * t
    Vec3 at(double t) const { return origin + dir * t; }

    // 与平面的交点（t 值；无交返回 -1）
    // 平面由 point + normal 定义
    double intersect_plane(const Vec3& plane_point, const Vec3& plane_normal) const {
        double den = dir.dot(plane_normal);
        if (std::abs(den) < 1e-12) return -1;   // 平行
        return (plane_point.dot(plane_normal) - origin.dot(plane_normal)) / den;
    }
    // 与轴对齐包围盒（AABB）求交：返回是否相交，tmin/tmax 为进入/离开参数
    bool intersect_aabb(const Vec3& minb, const Vec3& maxb, double& tmin, double& tmax) const;

    // ---- self test ----
    static int self_test();
};

// 平面
struct Plane {
    Vec3 normal;   // 法线（单位向量）
    double d;      // 方程 normal·x = d

    Plane() : normal(0, 1, 0), d(0) {}
    Plane(const Vec3& n, double D) : normal(n.normalized()), d(D) {}
    // 由平面上一点+法线构造
    static Plane from_point_normal(const Vec3& p, const Vec3& n) {
        return Plane(n, n.dot(p));
    }
    // 点到平面带符号距离（法线方向为正）
    double signed_distance(const Vec3& p) const { return normal.dot(p) - d; }
    // 点是否在平面上
    bool contains(const Vec3& p, double eps = 1e-9) const {
        return std::abs(signed_distance(p)) < eps;
    }
    // 反射方向：v 关于平面的反射
    Vec3 reflect(const Vec3& v) const {
        return v - normal * (2 * v.dot(normal));
    }

    // ---- self test ----
    static int self_test();
};

} // namespace gfx
} // namespace nefu
