// nefuOS gfxmath —— 包围盒 aabb 与球 sphere
// 教学版：轴对齐包围盒（AABB）与包围球，用于碰撞检测与空间裁剪。
#pragma once
#include <cmath>
#include "gfxmath/vec3.h"

namespace nefu {
namespace gfx {

// 轴对齐包围盒
struct AABB {
    Vec3 minb, maxb;

    AABB() : minb(), maxb() {}
    AABB(const Vec3& a, const Vec3& b) : minb(a), maxb(b) {}

    // 是否包含点（含边界）
    bool contains(const Vec3& p) const {
        return p.x >= minb.x && p.x <= maxb.x &&
               p.y >= minb.y && p.y <= maxb.y &&
               p.z >= minb.z && p.z <= maxb.z;
    }
    // 是否与另一盒相交
    bool intersects(const AABB& o) const {
        return maxb.x >= o.minb.x && o.maxb.x >= minb.x &&
               maxb.y >= o.minb.y && o.maxb.y >= minb.y &&
               maxb.z >= o.minb.z && o.maxb.z >= minb.z;
    }
    // 合并两盒
    AABB merged(const AABB& o) const {
        return AABB(
            Vec3(minb.x < o.minb.x ? minb.x : o.minb.x,
                 minb.y < o.minb.y ? minb.y : o.minb.y,
                 minb.z < o.minb.z ? minb.z : o.minb.z),
            Vec3(maxb.x > o.maxb.x ? maxb.x : o.maxb.x,
                 maxb.y > o.maxb.y ? maxb.y : o.maxb.y,
                 maxb.z > o.maxb.z ? maxb.z : o.maxb.z));
    }
    // 中心 / 尺寸 / 体积
    Vec3 center() const { return (minb + maxb) * 0.5; }
    Vec3 size() const { return maxb - minb; }
    double volume() const { Vec3 s = size(); return s.x * s.y * s.z; }
    // 表面积
    double surface_area() const {
        Vec3 s = size();
        return 2 * (s.x * s.y + s.y * s.z + s.z * s.x);
    }
    // 点与盒最近距离（点在盒内为 0）
    double distance_to(const Vec3& p) const {
        double dx = p.x < minb.x ? minb.x - p.x : (p.x > maxb.x ? p.x - maxb.x : 0);
        double dy = p.y < minb.y ? minb.y - p.y : (p.y > maxb.y ? p.y - maxb.y : 0);
        double dz = p.z < minb.z ? minb.z - p.z : (p.z > maxb.z ? p.z - maxb.z : 0);
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // ---- self test ----
    static int self_test();
};

// 包围球
struct Sphere {
    Vec3 center;
    double r;

    Sphere() : center(), r(1) {}
    Sphere(const Vec3& c, double R) : center(c), r(R) {}

    // 包含点
    bool contains(const Vec3& p) const { return center.dist(p) <= r; }
    // 与球相交
    bool intersects(const Sphere& o) const {
        double d = center.dist(o.center);
        return d <= r + o.r;
    }
    // 体积
    double volume() const { return 4.0 / 3.0 * 3.14159265358979 * r * r * r; }
    // 包围盒（外接）
    AABB bounding_box() const {
        return AABB(Vec3(center.x - r, center.y - r, center.z - r),
                    Vec3(center.x + r, center.y + r, center.z + r));
    }
    // 点与球面最近距离（内部为 0）
    double distance_to(const Vec3& p) const {
        double d = center.dist(p);
        return d > r ? d - r : 0;
    }

    // ---- self test ----
    static int self_test();
};

} // namespace gfx
} // namespace nefu
