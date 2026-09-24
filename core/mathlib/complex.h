// nefuOS mathlib —— 复数 complex
// 教学版：a + bi，四则运算 + 共轭 + 模 + 极坐标，用 cmath 求 sin/cos。
#pragma once
#include <cmath>

namespace nefu {
namespace mathx {

// 复数
struct Complex {
    double re;   // 实部
    double im;   // 虚部

    Complex() : re(0), im(0) {}
    Complex(double r, double i = 0) : re(r), im(i) {}

    Complex& add(const Complex& b);
    Complex& sub(const Complex& b);
    Complex& mul(const Complex& b);
    Complex& div(const Complex& b);
    double   mag() const { return std::sqrt(re * re + im * im); }
    double   arg() const { return std::atan2(im, re); }          // 辐角
    Complex  conj() const { return Complex(re, -im); }           // 共轭
    Complex  scaled(double k) const { return Complex(re * k, im * k); }
    bool     near(const Complex& b, double eps = 1e-9) const;

    static Complex from_polar(double r, double theta);           // 极坐标
};

inline Complex operator+(const Complex& a, const Complex& b) { Complex r = a; return r.add(b); }
inline Complex operator-(const Complex& a, const Complex& b) { Complex r = a; return r.sub(b); }
inline Complex operator*(const Complex& a, const Complex& b) { Complex r = a; return r.mul(b); }
inline Complex operator/(const Complex& a, const Complex& b) { Complex r = a; return r.div(b); }

int complex_self_test();

} // namespace mathx
} // namespace nefu
