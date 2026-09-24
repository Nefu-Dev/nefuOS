// nefuOS mathlib —— 多项式 polynomial
// 教学版：系数数组（低次在前），支持求值/加减乘/求导/综合除法。
#pragma once
#include <vector>
#include <cstdint>

namespace nefu {
namespace mathx {

// 一元多项式 p(x) = c[0] + c[1]x + c[2]x^2 + ...
struct Polynomial {
    std::vector<double> c;   // 系数，低次在前；去掉尾部零后存储

    Polynomial() {}
    explicit Polynomial(const std::vector<double>& coeffs);
    static Polynomial mono(double coef, int degree);   // 单项式 c*x^k

    int degree() const;                       // 最高次数（-1 表示零多项式）
    double eval(double x) const;              // 霍纳求值
    Polynomial& add(const Polynomial& b);
    Polynomial& sub(const Polynomial& b);
    Polynomial mul(const Polynomial& b) const;
    Polynomial derivative() const;            // 求导
    Polynomial& trim();                       // 去掉尾部零系数
    double coeff(int k) const;                // 第 k 次系数（越界返回 0）
};

int polynomial_self_test();

} // namespace mathx
} // namespace nefu
