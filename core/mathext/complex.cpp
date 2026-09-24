#pragma GCC optimize("no-tree-loop-distribute-patterns")
// nefuOS 数学扩展库 —— 复数模块实现
#include "complex.h"
#include <cmath>
#include <cstddef>

namespace nefu {
namespace mathext {

// ---------------- 近似判据 ----------------
// 混合相对/绝对误差：对接近 0 的量用绝对误差，对大数用相对误差。
// eps 默认 1e-9，与 double 的机器精度(2e-16)留出足够裕量。
bool d_close(double a, double b, double eps) {
    double diff = a - b;
    if (diff < 0) diff = -diff;
    double ref = a;
    if (ref < 0) ref = -ref;
    if (b < 0 && (-b) > ref) ref = -b;
    if (ref < 1.0) ref = 1.0;
    return diff <= eps * ref;
}

bool c_close(const Complex& a, const Complex& b, double eps) {
    return d_close(a.re, b.re, eps) && d_close(a.im, b.im, eps);
}

// ---------------- 四则运算 ----------------
Complex c_add(const Complex& a, const Complex& b) {
    return Complex(a.re + b.re, a.im + b.im);
}

Complex c_sub(const Complex& a, const Complex& b) {
    return Complex(a.re - b.re, a.im - b.im);
}

Complex c_mul(const Complex& a, const Complex& b) {
    // 展开 (a+bi)(c+di)：实部 ac-bd，虚部 ad+bc
    return Complex(a.re * b.re - a.im * b.im,
                   a.re * b.im + a.im * b.re);
}

Complex c_div(const Complex& a, const Complex& b) {
    // 分子分母同乘分母共轭：
    // (a+bi)/(c+di) = (a+bi)(c-di)/(c^2+d^2)
    double denom = b.re * b.re + b.im * b.im;
    if (denom < 1e-300) denom = 1e-300;   // 除零保护，避免 inf
    return Complex((a.re * b.re + a.im * b.im) / denom,
                   (a.im * b.re - a.re * b.im) / denom);
}

// ---------------- 标量 ----------------
Complex c_scale(const Complex& a, double s) {
    return Complex(a.re * s, a.im * s);
}

Complex c_neg(const Complex& a) {
    return Complex(-a.re, -a.im);
}

// ---------------- 共轭 / 模 / 辐角 ----------------
Complex c_conj(const Complex& a) {
    return Complex(a.re, -a.im);
}

double c_norm2(const Complex& a) {
    return a.re * a.re + a.im * a.im;
}

double c_abs(const Complex& a) {
    return std::sqrt(c_norm2(a));
}

double c_arg(const Complex& a) {
    return std::atan2(a.im, a.re);
}

// ---------------- 极坐标 ----------------
Complex c_from_polar(double r, double theta) {
    return Complex(r * std::cos(theta), r * std::sin(theta));
}

void c_to_polar(const Complex& a, double& out_r, double& out_theta) {
    out_r = c_abs(a);
    out_theta = c_arg(a);
}

// ---------------- 初等函数 ----------------
Complex c_exp(const Complex& a) {
    // e^(x+iy) = e^x (cos y + i sin y)
    double er = std::exp(a.re);
    return Complex(er * std::cos(a.im), er * std::sin(a.im));
}

Complex c_log(const Complex& a) {
    // ln z = ln|z| + i Arg(z)，主值取 (-pi, pi] 分支
    double r = c_abs(a);
    if (r < 1e-300) r = 1e-300;
    return Complex(std::log(r), c_arg(a));
}

Complex c_pow_int(const Complex& a, int n) {
    // 快速幂：O(log n) 次乘法，符号 n 取倒数处理。
    Complex result(1.0);
    Complex base = a;
    int exp = n;
    bool inv = false;
    if (exp < 0) { inv = true; exp = -exp; }
    while (exp > 0) {
        if (exp & 1) result = c_mul(result, base);
        base = c_mul(base, base);
        exp >>= 1;
    }
    if (inv) result = c_div(Complex(1.0), result);
    return result;
}

Complex c_pow(const Complex& a, const Complex& w) {
    // z^w = exp(w * log z)（主值分支）
    return c_exp(c_mul(w, c_log(a)));
}

Complex c_sqrt(const Complex& a) {
    // z^(1/2)
    return c_pow(a, Complex(0.5));
}

Complex c_sin(const Complex& a) {
    // sin(x+iy) = sin x cosh y + i cos x sinh y
    return Complex(std::sin(a.re) * std::cosh(a.im),
                   std::cos(a.re) * std::sinh(a.im));
}

Complex c_cos(const Complex& a) {
    // cos(x+iy) = cos x cosh y - i sin x sinh y
    return Complex(std::cos(a.re) * std::cosh(a.im),
                  -std::sin(a.re) * std::sinh(a.im));
}

// ---------------- 单位根 ----------------
Complex unit_root(int n, int k) {
    // w_k = exp(2*pi*i*k/n) = cos(2*pi*k/n) + i sin(2*pi*k/n)
    double theta = 2.0 * MATH_PI * (double)k / (double)n;
    return Complex(std::cos(theta), std::sin(theta));
}

// ---------------- 双曲/反三角 ----------------
Complex c_tanh(const Complex& a) {
    // tanh(z) = sinh(z)/cosh(z)
    Complex s = c_sin(a);   // 复用现有
    Complex c = c_cos(a);
    return c_div(s, c);
}

Complex c_asinh(const Complex& a) {
    // asinh(z) = log(z + sqrt(z^2 + 1))
    Complex z2 = c_mul(a, a);
    Complex one(1, 0);
    return c_log(c_add(a, c_sqrt(c_add(z2, one))));
}

Complex c_acosh(const Complex& a) {
    // acosh(z) = log(z + sqrt(z^2 - 1))
    Complex z2 = c_mul(a, a);
    Complex one(1, 0);
    return c_log(c_add(a, c_sqrt(c_sub(z2, one))));
}

Complex c_atanh(const Complex& a) {
    // atanh(z) = 0.5 * log((1+z)/(1-z))
    Complex one(1, 0);
    Complex num = c_add(one, a);
    Complex den = c_sub(one, a);
    Complex q = c_div(num, den);
    Complex half(0.5, 0);
    return c_scale(c_log(q), 0.5);
}

double c_log10_real(double x) {
    if (x <= 0) return 0.0;
    return std::log(x) / std::log(10.0);
}
// ---------------- 自检 ----------------
int complex_self_test() {
    int fails = 0;
    const double EPS = 1e-9;

    // 1. 已知值：(1+2i)*(3+4i) = -5+10i
    Complex r1 = c_mul(Complex(1, 2), Complex(3, 4));
    if (!c_close(r1, Complex(-5, 10), EPS)) fails++;

    // 2. (1+i)/(1-i) = i
    Complex r2 = c_div(Complex(1, 1), Complex(1, -1));
    if (!c_close(r2, Complex(0, 1), EPS)) fails++;

    // 3. 共轭：conj(3-4i)=3+4i
    if (!c_close(c_conj(Complex(3, -4)), Complex(3, 4), EPS)) fails++;

    // 4. 模：|3+4i| = 5
    if (!d_close(c_abs(Complex(3, 4)), 5.0, EPS)) fails++;

    // 5. 欧拉恒等式：exp(i*pi) + 1 = 0
    Complex e = c_exp(Complex(0, MATH_PI));
    if (!c_close(c_add(e, Complex(1)), Complex(0), EPS)) fails++;

    // 6. 对数逆运算：log(exp(2+3i)) ≈ 2+3i
    Complex lg = c_log(c_exp(Complex(2, 3)));
    if (!c_close(lg, Complex(2, 3), EPS)) fails++;

    // 7. 极坐标往返：z = from_polar(|z|, arg(z))
    Complex z(3, 4);
    double rr, th;
    c_to_polar(z, rr, th);
    if (!c_close(c_from_polar(rr, th), z, EPS)) fails++;

    // 8. 整数幂：(1+i)^2 = 2i
    if (!c_close(c_pow_int(Complex(1, 1), 2), Complex(0, 2), EPS)) fails++;

    // 9. 平方根：sqrt(-1) = i（主值）
    Complex sq = c_sqrt(Complex(-1));
    if (!c_close(sq, Complex(0, 1), EPS)) fails++;

    // 10. 单位根：n=4, k=1 -> i；k=2 -> -1；4 个根之和为 0
    if (!c_close(unit_root(4, 1), Complex(0, 1), EPS)) fails++;
    if (!c_close(unit_root(4, 2), Complex(-1), EPS)) fails++;
    Complex sum(0, 0);
    for (int k = 0; k < 4; k++) sum = c_add(sum, unit_root(4, k));
    if (!c_close(sum, Complex(0), EPS)) fails++;

    return fails;
}

} // namespace mathext
} // namespace nefu
