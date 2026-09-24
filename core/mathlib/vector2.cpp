// nefuOS mathlib —— 二维向量实现 + 自测
#include "mathlib/vector2.h"
#include <cstdio>

namespace nefu {
namespace mathx {

Vec2 Vec2::normalized() const {
    double l = len();
    if (l == 0) return *this;
    return Vec2(x / l, y / l);
}

double Vec2::angle_to(const Vec2& b) const {
    double d = dot(b) / (len() * b.len());
    if (d > 1) d = 1;
    if (d < -1) d = -1;
    return std::acos(d);
}

Vec2 Vec2::rotated(double theta) const {
    double c = std::cos(theta), s = std::sin(theta);
    return Vec2(x * c - y * s, x * s + y * c);
}

double Vec2::dist(const Vec2& b) const {
    double dx = x - b.x, dy = y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
}

int vector2_self_test() {
    g_fails = 0;
    {
        Vec2 a(1, 0), b(0, 1);
        expect("vec-dot", a.dot(b) == 0);
        expect("vec-dot-para", a.dot(a) == 1);
        expect("vec-len", Vec2(3, 4).len() == 5);
        expect("vec-cross", a.cross(b) == 1);
        expect("vec-cross-neg", b.cross(a) == -1);
        expect("vec-angle", (a.angle_to(b) - 3.14159265358979 / 2) < 1e-9);
    }
    {
        Vec2 a(1, 2), b(3, 4);
        Vec2 s = a + b;
        expect("vec-add", s.x == 4 && s.y == 6);
        Vec2 d = a - b;
        expect("vec-sub", d.x == -2 && d.y == -2);
        expect("vec-dist", Vec2(0, 0).dist(a) == std::sqrt(5.0));
    }
    {
        Vec2 u = Vec2(3, 4).normalized();
        expect("vec-normalized", std::abs(u.len() - 1.0) < 1e-12);
        Vec2 v = Vec2(1, 0).rotated(3.14159265358979 / 2);
        expect("vec-rotate", std::abs(v.x) < 1e-9 && std::abs(v.y - 1) < 1e-9);
    }
    return g_fails;
}

} // namespace mathx
} // namespace nefu
