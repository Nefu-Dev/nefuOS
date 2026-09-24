// nefuOS gfxmath —— 射线与平面实现 + 自测
#include "gfxmath/ray.h"
#include <cstdio>
#include <algorithm>

namespace nefu {
namespace gfx {

bool Ray::intersect_aabb(const Vec3& minb, const Vec3& maxb, double& tmin, double& tmax) const {
    double t1 = (minb.x - origin.x) / dir.x, t2 = (maxb.x - origin.x) / dir.x;
    if (t1 > t2) { double t = t1; t1 = t2; t2 = t; }
    double tnear = t1, tfar = t2;
    t1 = (minb.y - origin.y) / dir.y; t2 = (maxb.y - origin.y) / dir.y;
    if (t1 > t2) { double t = t1; t1 = t2; t2 = t; }
    tnear = std::max(tnear, t1);
    tfar = std::min(tfar, t2);
    t1 = (minb.z - origin.z) / dir.z; t2 = (maxb.z - origin.z) / dir.z;
    if (t1 > t2) { double t = t1; t1 = t2; t2 = t; }
    tnear = std::max(tnear, t1);
    tfar = std::min(tfar, t2);
    tmin = tnear; tmax = tfar;
    if (tnear > tfar) return false;
    if (tfar < 0) return false;   // 完全在反方向
    return true;
}

// ---- self test ----
int Ray::self_test() {
    int fails = 0;
    // 1. 参数位置
    {
        Ray r(Vec3(1, 0, 0), Vec3(0, 1, 0));
        Vec3 p = r.at(5);
        if (p.x != 1 || p.y != 5 || p.z != 0) fails++;
    }
    // 2. 射线与平面求交（原点朝 Z 方向，平面 z=10）
    {
        Ray r(Vec3(0, 0, 0), Vec3(0, 0, 1));
        double t = r.intersect_plane(Vec3(0, 0, 10), Vec3(0, 0, 1));
        if (std::abs(t - 10) > 1e-9) fails++;
        Vec3 hit = r.at(t);
        if (std::abs(hit.z - 10) > 1e-9) fails++;
    }
    // 3. 平行射线无交
    {
        Ray r(Vec3(0, 0, 0), Vec3(1, 0, 0));
        double t = r.intersect_plane(Vec3(0, 0, 10), Vec3(0, 0, 1));
        if (t != -1) fails++;
    }
    // 4. AABB 求交
    {
        Ray r(Vec3(0, 0, 0), Vec3(1, 1, 1));
        double t0, t1;
        bool hit = r.intersect_aabb(Vec3(1, 1, 1), Vec3(2, 2, 2), t0, t1);
        if (!hit) fails++;
        if (t0 < 0.99 || t0 > 1.01) fails++;   // 进入点约 (1,1,1)
        // 远离的盒不应命中
        Ray r2(Vec3(0, 0, 0), Vec3(1, 0, 0));
        bool miss = r2.intersect_aabb(Vec3(5, 5, 5), Vec3(6, 6, 6), t0, t1);
        if (miss) fails++;
    }
    return fails;
}

int Plane::self_test() {
    int fails = 0;
    // 1. 点到平面距离
    {
        // 平面 z=0：点 (3,4,5) 距离 5
        Plane p(Vec3(0, 0, 1), 0);
        if (std::abs(p.signed_distance(Vec3(3, 4, 5)) - 5) > 1e-9) fails++;
        if (std::abs(p.signed_distance(Vec3(1, 2, -3)) + 3) > 1e-9) fails++;
    }
    // 2. 由点+法线构造
    {
        Plane p = Plane::from_point_normal(Vec3(0, 0, 5), Vec3(0, 0, 1));
        if (!p.contains(Vec3(0, 0, 5))) fails++;
        if (p.contains(Vec3(0, 0, 6))) fails++;
    }
    // 3. 反射
    {
        // 水平面，斜向下的向量反射后向上
        Plane p(Vec3(0, 1, 0), 0);
        Vec3 r = p.reflect(Vec3(1, -1, 0));
        if (std::abs(r.x - 1) > 1e-9 || std::abs(r.y - 1) > 1e-9) fails++;
        // 垂直入射原路返回
        Vec3 r2 = p.reflect(Vec3(0, -1, 0));
        if (std::abs(r2.y - 1) > 1e-9) fails++;
    }
    // 4. 法线归一化
    {
        Plane p(Vec3(0, 2, 0), 3);   // 非单位法线
        if (std::abs(p.normal.len() - 1) > 1e-9) fails++;
    }
    return fails;
}

} // namespace gfx
} // namespace nefu
