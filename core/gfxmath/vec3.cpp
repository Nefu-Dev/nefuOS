// nefuOS gfxmath —— 三维向量实现 + 自测
#include "gfxmath/vec3.h"
#include <cstdio>

namespace nefu {
namespace gfx {

// ---- self test ----
int Vec3::self_test() {
    int fails = 0;
    // 1. 基本运算
    {
        Vec3 a(1, 2, 3), b(4, 5, 6);
        Vec3 s = a + b;
        if (s.x != 5 || s.y != 7 || s.z != 9) fails++;
        Vec3 d = a - b;
        if (d.x != -3 || d.y != -3 || d.z != -3) fails++;
        Vec3 n = -a;
        if (n.x != -1 || n.y != -2 || n.z != -3) fails++;
        Vec3 m = a * 2;
        if (m.x != 2 || m.y != 4 || m.z != 6) fails++;
        Vec3 dv = a / 2;
        if (dv.x != 0.5 || dv.y != 1 || dv.z != 1.5) fails++;
    }
    // 2. 点积 / 叉积
    {
        Vec3 a(1, 0, 0), b(0, 1, 0);
        if (a.dot(b) != 0) fails++;
        Vec3 c = a.cross(b);
        if (c.x != 0 || c.y != 0 || c.z != 1) fails++;
        Vec3 cb = b.cross(a);
        if (cb.z != -1) fails++;
    }
    // 3. 长度 / 归一化
    {
        Vec3 a(3, 4, 0);
        if (a.len() != 5) fails++;
        Vec3 u = a.normalized();
        if (std::abs(u.len() - 1.0) > 1e-12) fails++;
        if (std::abs(u.x - 0.6) > 1e-9) fails++;
        Vec3 z(0, 0, 0);
        Vec3 zn = z.normalized();
        if (zn.x != 0 || zn.y != 0 || zn.z != 0) fails++;
    }
    // 4. 距离 / 夹角
    {
        Vec3 a(0, 0, 0), b(1, 0, 0);
        if (a.dist(b) != 1) fails++;
        if (std::abs(a.angle_to(b) - 3.14159265358979 / 2) > 1e-9) fails++;  // 垂直
        Vec3 c(1, 0, 0), d(1, 0, 0);
        if (std::abs(c.angle_to(d)) > 1e-9) fails++;   // 平行夹角 0
    }
    // 5. 插值
    {
        Vec3 a(0, 0, 0), b(10, 0, 0);
        Vec3 m = a.lerp(b, 0.5);
        if (m.x != 5) fails++;
        Vec3 e = a.lerp(b, 1.0);
        if (e.x != 10) fails++;
    }
    return fails;
}

} // namespace gfx
} // namespace nefu
