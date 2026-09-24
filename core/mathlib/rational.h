// nefuOS mathlib —— 分数 rational
// 教学版：分子/分母 + 约分（辗转相除）+ 四则运算 + 比较。
#pragma once
#include <cstdint>

namespace nefu {
namespace mathx {

// 有理数 a/b（b>0，始终约分）
struct Rational {
    int64_t num;   // 分子
    int64_t den;   // 分母（>0）

    Rational();                       // 0/1
    Rational(int64_t n, int64_t d = 1);
    explicit Rational(double x);      // 有限小数转分数（教学近似）

    Rational& reduce();               // 约分
    Rational& add(const Rational& b);
    Rational& sub(const Rational& b);
    Rational& mul(const Rational& b);
    Rational& div(const Rational& b);
    int cmp(const Rational& b) const;
    double to_double() const { return (double)num / (double)den; }
};

inline Rational operator+(const Rational& a, const Rational& b) { Rational r = a; return r.add(b); }
inline Rational operator-(const Rational& a, const Rational& b) { Rational r = a; return r.sub(b); }
inline Rational operator*(const Rational& a, const Rational& b) { Rational r = a; return r.mul(b); }
inline Rational operator/(const Rational& a, const Rational& b) { Rational r = a; return r.div(b); }
inline bool operator==(const Rational& a, const Rational& b) { return a.cmp(b) == 0; }
inline bool operator<(const Rational& a, const Rational& b)  { return a.cmp(b) < 0; }

int rational_self_test();

} // namespace mathx
} // namespace nefu
