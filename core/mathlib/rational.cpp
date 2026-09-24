// nefuOS mathlib —— 分数实现 + 自测
#include "mathlib/rational.h"
#include <cstdio>
#include <cstdlib>

namespace nefu {
namespace mathx {

static int64_t igcd(int64_t a, int64_t b) {
    a = a < 0 ? -a : a; b = b < 0 ? -b : b;
    while (b) { int64_t t = a % b; a = b; b = t; }
    return a;
}

Rational::Rational() : num(0), den(1) {}

Rational::Rational(int64_t n, int64_t d) : num(n), den(d) {
    if (den == 0) { den = 1; num = 0; }
    reduce();
}

Rational::Rational(double x) {
    // 教学近似：转成分数（有限精度）
    int64_t n = (int64_t)(x * 1e9 + (x >= 0 ? 0.5 : -0.5));
    num = n; den = 1000000000LL;
    reduce();
}

Rational& Rational::reduce() {
    if (num == 0) { den = 1; return *this; }
    if (den < 0) { num = -num; den = -den; }
    int64_t g = igcd(num, den);
    if (g > 1) { num /= g; den /= g; }
    return *this;
}

Rational& Rational::add(const Rational& b) {
    // a/b + c/d = (ad + cb) / bd
    int64_t n = num * b.den + b.num * den;
    int64_t d = den * b.den;
    num = n; den = d;
    return reduce();
}

Rational& Rational::sub(const Rational& b) {
    Rational nb = b;
    nb.num = -nb.num;
    return add(nb);
}

Rational& Rational::mul(const Rational& b) {
    num = num * b.num;
    den = den * b.den;
    return reduce();
}

Rational& Rational::div(const Rational& b) {
    if (b.num == 0) { num = 0; den = 1; return *this; }
    num = num * b.den;
    den = den * b.num;
    return reduce();
}

int Rational::cmp(const Rational& b) const {
    // 交叉相乘：a/b vs c/d -> ad vs cb
    int64_t l = num * b.den;
    int64_t r = b.num * den;
    if (l == r) return 0;
    return l < r ? -1 : 1;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
}

int rational_self_test() {
    g_fails = 0;
    {
        Rational a(1, 2), b(1, 3);
        Rational s = a + b;
        expect("rational-add", s.num == 5 && s.den == 6);
        Rational d = a - b;
        expect("rational-sub", d.num == 1 && d.den == 6);
        Rational m = a * b;
        expect("rational-mul", m.num == 1 && m.den == 6);
        Rational q = a / b;
        expect("rational-div", q.num == 3 && q.den == 2);
    }
    {
        Rational a(4, 6);
        expect("rational-reduce", a.num == 2 && a.den == 3);
        Rational b(0, 5);
        expect("rational-zero", b.num == 0 && b.den == 1);
        Rational c(2, 4), d(1, 2);
        expect("rational-eq", c == d);
        Rational e(-3, 4), f(3, -4);
        expect("rational-neg-eq", e.num == -3 && e.den == 4 && f.num == -3 && f.den == 4);
        expect("rational-to-double", (a.to_double() - 2.0 / 3.0) < 1e-12);
    }
    {
        Rational x(0.5);
        expect("rational-from-double", x.num == 1 && x.den == 2);
        Rational y(0.25);
        expect("rational-from-double2", y.num == 1 && y.den == 4);
    }
    return g_fails;
}

} // namespace mathx
} // namespace nefu
