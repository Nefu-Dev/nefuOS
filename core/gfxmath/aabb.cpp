// nefuOS gfxmath —— 包围盒与包围球实现 + 自测
#include "gfxmath/aabb.h"
#include <cstdio>

namespace nefu {
namespace gfx {

// ---- self test ----
int AABB::self_test() {
    int fails = 0;
    // 1. 包含点
    {
        AABB b(Vec3(0, 0, 0), Vec3(2, 2, 2));
        if (!b.contains(Vec3(1, 1, 1))) fails++;
        if (!b.contains(Vec3(0, 0, 0))) fails++;
        if (b.contains(Vec3(3, 1, 1))) fails++;
    }
    // 2. 相交
    {
        AABB a(Vec3(0, 0, 0), Vec3(2, 2, 2));
        AABB b(Vec3(1, 1, 1), Vec3(3, 3, 3));
        AABB c(Vec3(5, 5, 5), Vec3(6, 6, 6));
        if (!a.intersects(b)) fails++;
        if (a.intersects(c)) fails++;
    }
    // 3. 合并 / 中心 / 体积
    {
        AABB a(Vec3(0, 0, 0), Vec3(1, 1, 1));
        AABB b(Vec3(2, 2, 2), Vec3(3, 3, 3));
        AABB m = a.merged(b);
        if (m.minb.x != 0 || m.maxb.x != 3) fails++;
        Vec3 c = m.center();
        if (c.x != 1.5 || c.y != 1.5 || c.z != 1.5) fails++;
        if (std::abs(m.volume() - 27) > 1e-9) fails++;
        if (std::abs(m.surface_area() - 54) > 1e-9) fails++;
    }
    // 4. 距离
    {
        AABB b(Vec3(0, 0, 0), Vec3(1, 1, 1));
        if (b.distance_to(Vec3(0.5, 0.5, 0.5)) != 0) fails++;   // 内部
        if (std::abs(b.distance_to(Vec3(2, 0.5, 0.5)) - 1) > 1e-9) fails++;
        if (std::abs(b.distance_to(Vec3(2, 2, 2)) - std::sqrt(3.0)) > 1e-9) fails++;
    }
    return fails;
}

int Sphere::self_test() {
    int fails = 0;
    // 1. 包含
    {
        Sphere s(Vec3(0, 0, 0), 5);
        if (!s.contains(Vec3(3, 4, 0))) fails++;    // 距离 5
        if (s.contains(Vec3(6, 0, 0))) fails++;
    }
    // 2. 相交
    {
        Sphere a(Vec3(0, 0, 0), 3);
        Sphere b(Vec3(5, 0, 0), 3);    // 中心距 5 < 6
        Sphere c(Vec3(10, 0, 0), 3);   // 中心距 10 > 6
        if (!a.intersects(b)) fails++;
        if (a.intersects(c)) fails++;
    }
    // 3. 体积与包围盒
    {
        Sphere s(Vec3(1, 2, 3), 2);
        double vol = s.volume();
        if (std::abs(vol - 4.0 / 3.0 * 3.14159265358979 * 8) > 1e-9) fails++;
        AABB b = s.bounding_box();
        if (b.minb.x != -1 || b.maxb.x != 3) fails++;
        if (b.minb.y != 0 || b.maxb.y != 4) fails++;
    }
    // 4. 距离
    {
        Sphere s(Vec3(0, 0, 0), 2);
        if (s.distance_to(Vec3(1, 0, 0)) != 0) fails++;
        if (std::abs(s.distance_to(Vec3(5, 0, 0)) - 3) > 1e-9) fails++;
    }
    return fails;
}

} // namespace gfx
} // namespace nefu
