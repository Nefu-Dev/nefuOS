// nefuOS gfxmath —— 三维向量 vec3
// 教学版：三维空间向量运算（点、叉积、长度、归一化、插值）。
// 用于 3D 图形学中的坐标、方向、法线计算。class / STL / cmath / 中文注释。
#pragma once
#include <cmath>

namespace nefu {
namespace gfx {

// 三维向量
struct Vec3 {
    double x, y, z;

    Vec3() : x(0), y(0), z(0) {}
    Vec3(double X, double Y, double Z) : x(X), y(Y), z(Z) {}

    // 运算
    Vec3 operator+(const Vec3& b) const { return Vec3(x + b.x, y + b.y, z + b.z); }
    Vec3 operator-(const Vec3& b) const { return Vec3(x - b.x, y - b.y, z - b.z); }
    Vec3 operator-() const { return Vec3(-x, -y, -z); }
    Vec3 operator*(double s) const { return Vec3(x * s, y * s, z * s); }
    Vec3 operator/(double s) const { double r = s != 0 ? 1.0 / s : 0; return Vec3(x * r, y * r, z * r); }
    Vec3& operator+=(const Vec3& b) { x += b.x; y += b.y; z += b.z; return *this; }
    Vec3& operator-=(const Vec3& b) { x -= b.x; y -= b.y; z -= b.z; return *this; }

    // 点积
    double dot(const Vec3& b) const { return x * b.x + y * b.y + z * b.z; }
    // 叉积
    Vec3 cross(const Vec3& b) const {
        return Vec3(y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x);
    }
    // 长度平方 / 长度
    double len2() const { return x * x + y * y + z * z; }
    double len() const { return std::sqrt(len2()); }
    // 归一化（零向量原样返回）
    Vec3 normalized() const {
        double l = len();
        if (l == 0) return *this;
        return Vec3(x / l, y / l, z / l);
    }
    // 与 b 的距离
    double dist(const Vec3& b) const { return (*this - b).len(); }
    // 与 b 的夹角（弧度）
    double angle_to(const Vec3& b) const {
        double d = dot(b) / (len() * b.len());
        if (d > 1) d = 1;
        if (d < -1) d = -1;
        return std::acos(d);
    }
    // 线性插值：this*(1-t) + b*t
    Vec3 lerp(const Vec3& b, double t) const {
        return Vec3(x + (b.x - x) * t, y + (b.y - y) * t, z + (b.z - z) * t);
    }

    // ---- self test ----
    static int self_test();
};

} // namespace gfx
} // namespace nefu
