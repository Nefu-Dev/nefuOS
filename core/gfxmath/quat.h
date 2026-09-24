// nefuOS gfxmath —— 四元数 quat
// 教学版：旋转的四元数表示，用于避免欧拉角万向锁。
// 支持构造、乘法、旋转向量、球面插值（slerp）。
#pragma once
#include <cmath>
#include "gfxmath/vec3.h"

namespace nefu {
namespace gfx {

// 四元数（w + xi + yj + zk）
struct Quat {
    double w, x, y, z;

    Quat() : w(1), x(0), y(0), z(0) {}
    Quat(double W, double X, double Y, double Z) : w(W), x(X), y(Y), z(Z) {}

    // 由轴-角构造（axis 单位向量，angle 弧度）
    static Quat from_axis_angle(const Vec3& axis, double angle);
    // 由欧拉角构造（依次绕 Z、Y、X，弧度）
    static Quat from_euler(double rx, double ry, double rz);

    // 乘法（复合旋转）：this * q
    Quat operator*(const Quat& q) const;
    // 共轭（逆）
    Quat conjugate() const { return Quat(w, -x, -y, -z); }
    // 归一化
    Quat normalized() const {
        double l = std::sqrt(w * w + x * x + y * y + z * z);
        if (l == 0) return Quat(1, 0, 0, 0);
        return Quat(w / l, x / l, y / l, z / l);
    }
    // 模长
    double norm() const { return std::sqrt(w * w + x * x + y * y + z * z); }

    // 旋转向量 v
    Vec3 rotate(const Vec3& v) const;
    // 转成 3x3 旋转矩阵（按行存 9 个元素）
    void to_matrix(double out[9]) const;

    // 球面插值：q 与 this 之间 t∈[0,1]
    Quat slerp(const Quat& q, double t) const;

    // ---- self test ----
    static int self_test();
};

} // namespace gfx
} // namespace nefu
