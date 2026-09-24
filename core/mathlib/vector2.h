// nefuOS mathlib —— 二维向量 vector2
// 教学版：几何向量，点积/叉积(标量)/长度/夹角/旋转。
#pragma once
#include <cmath>

namespace nefu {
namespace mathx {

// 2D 向量
struct Vec2 {
    double x, y;

    Vec2() : x(0), y(0) {}
    Vec2(double x_, double y_) : x(x_), y(y_) {}

    Vec2& add(const Vec2& b) { x += b.x; y += b.y; return *this; }
    Vec2& sub(const Vec2& b) { x -= b.x; y -= b.y; return *this; }
    Vec2& scaled(double k) { x *= k; y *= k; return *this; }
    double dot(const Vec2& b) const { return x * b.x + y * b.y; }
    double cross(const Vec2& b) const { return x * b.y - y * b.x; }  // 标量叉积
    double len() const { return std::sqrt(x * x + y * y); }
    Vec2   normalized() const;                    // 单位向量（零向量返回自身）
    double angle_to(const Vec2& b) const;         // 夹角（弧度）
    Vec2   rotated(double theta) const;           // 逆时针旋转
    double dist(const Vec2& b) const;             // 距离
};

inline Vec2 operator+(const Vec2& a, const Vec2& b) { Vec2 r = a; return r.add(b); }
inline Vec2 operator-(const Vec2& a, const Vec2& b) { Vec2 r = a; return r.sub(b); }

int vector2_self_test();

} // namespace mathx
} // namespace nefu
