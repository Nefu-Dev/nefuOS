// nefuOS gfxmath —— 四元数实现 + 自测
#include "gfxmath/quat.h"
#include <cstdio>

namespace nefu {
namespace gfx {

Quat Quat::from_axis_angle(const Vec3& axis, double angle) {
    Vec3 a = axis.normalized();
    double s = std::sin(angle * 0.5);
    return Quat(std::cos(angle * 0.5), a.x * s, a.y * s, a.z * s);
}

Quat Quat::from_euler(double rx, double ry, double rz) {
    // 先 Z 后 Y 后 X（对应常见欧拉角约定）
    Quat qz = from_axis_angle(Vec3(0, 0, 1), rz);
    Quat qy = from_axis_angle(Vec3(0, 1, 0), ry);
    Quat qx = from_axis_angle(Vec3(1, 0, 0), rx);
    return qx * qy * qz;
}

Quat Quat::operator*(const Quat& q) const {
    return Quat(
        w * q.w - x * q.x - y * q.y - z * q.z,
        w * q.x + x * q.w + y * q.z - z * q.y,
        w * q.y - x * q.z + y * q.w + z * q.x,
        w * q.z + x * q.y - y * q.x + z * q.w);
}

Vec3 Quat::rotate(const Vec3& v) const {
    // v' = q * v * q^-1
    Quat p(0, v.x, v.y, v.z);
    Quat r = (*this) * p * conjugate();
    return Vec3(r.x, r.y, r.z);
}

void Quat::to_matrix(double out[9]) const {
    double w2 = w * w, x2 = x * x, y2 = y * y, z2 = z * z;
    out[0] = w2 + x2 - y2 - z2; out[1] = 2 * (x * y - w * z);     out[2] = 2 * (x * z + w * y);
    out[3] = 2 * (x * y + w * z); out[4] = w2 - x2 + y2 - z2;     out[5] = 2 * (y * z - w * x);
    out[6] = 2 * (x * z - w * y); out[7] = 2 * (y * z + w * x);   out[8] = w2 - x2 - y2 + z2;
}

Quat Quat::slerp(const Quat& q, double t) const {
    double dot = w * q.w + x * q.x + y * q.y + z * q.z;
    double qw = q.w, qx = q.x, qy = q.y, qz = q.z;
    if (dot < 0) { dot = -dot; qw = -qw; qx = -qx; qy = -qy; qz = -qz; }
    const double eps = 1e-8;
    if (1.0 - dot < eps) {
        // 接近平行：线性插值后归一化
        Quat r(w + (qw - w) * t, x + (qx - x) * t, y + (qy - y) * t, z + (qz - z) * t);
        return r.normalized();
    }
    double theta = std::acos(dot);
    double s0 = std::sin((1 - t) * theta) / std::sin(theta);
    double s1 = std::sin(t * theta) / std::sin(theta);
    return Quat(w * s0 + qw * s1, x * s0 + qx * s1, y * s0 + qy * s1, z * s0 + qz * s1);
}

// ---- self test ----
int Quat::self_test() {
    int fails = 0;
    // 1. 单位四元数旋转不变
    {
        Quat id;
        Vec3 v(1, 2, 3);
        Vec3 r = id.rotate(v);
        if (std::abs(r.x - 1) > 1e-9 || std::abs(r.y - 2) > 1e-9 || std::abs(r.z - 3) > 1e-9) fails++;
    }
    // 2. 绕 Z 轴 90 度旋转 X 轴 -> Y 轴
    {
        Quat q = Quat::from_axis_angle(Vec3(0, 0, 1), 3.14159265358979 / 2);
        Vec3 r = q.rotate(Vec3(1, 0, 0));
        if (std::abs(r.x) > 1e-9 || std::abs(r.y - 1) > 1e-9) fails++;
        // 反向旋转还原
        Vec3 back = q.conjugate().rotate(r);
        if (std::abs(back.x - 1) > 1e-9 || std::abs(back.y) > 1e-9) fails++;
    }
    // 3. 模长守恒
    {
        Quat q = Quat::from_axis_angle(Vec3(1, 1, 0), 0.7);
        Vec3 v(3, 4, 5);
        Vec3 r = q.rotate(v);
        if (std::abs(r.len() - v.len()) > 1e-9) fails++;
    }
    // 4. 矩阵形式一致性
    {
        Quat q = Quat::from_axis_angle(Vec3(0, 1, 0), 0.5);
        double m[9];
        q.to_matrix(m);
        Vec3 v(1, 0, 0);
        Vec3 rq = q.rotate(v);
        // 用矩阵乘（行主序）
        double mx = m[0] * v.x + m[1] * v.y + m[2] * v.z;
        double my = m[3] * v.x + m[4] * v.y + m[5] * v.z;
        double mz = m[6] * v.x + m[7] * v.y + m[8] * v.z;
        if (std::abs(mx - rq.x) > 1e-9 || std::abs(my - rq.y) > 1e-9 || std::abs(mz - rq.z) > 1e-9) fails++;
    }
    // 5. slerp 端点与中点
    {
        Quat a = Quat::from_axis_angle(Vec3(0, 0, 1), 0);
        Quat b = Quat::from_axis_angle(Vec3(0, 0, 1), 1.0);
        Quat mid = a.slerp(b, 0.5);
        // 中点角度约 0.5
        Vec3 r = mid.rotate(Vec3(1, 0, 0));
        double ang = std::atan2(r.y, r.x);
        if (std::abs(ang - 0.5) > 1e-6) fails++;
        Quat e = a.slerp(b, 1.0);
        if (std::abs(e.w - b.w) > 1e-9 || std::abs(e.z - b.z) > 1e-9) fails++;
    }
    // 6. 欧拉角构造旋转
    {
        Quat q = Quat::from_euler(0, 0, 3.14159265358979 / 2);
        Vec3 r = q.rotate(Vec3(1, 0, 0));
        if (std::abs(r.x) > 1e-9 || std::abs(r.y - 1) > 1e-9) fails++;
    }
    return fails;
}

} // namespace gfx
} // namespace nefu
