// nefuOS mathlib —— 多项式实现 + 自测
#include "mathlib/polynomial.h"
#include <cstdio>

namespace nefu {
namespace mathx {

Polynomial::Polynomial(const std::vector<double>& coeffs) : c(coeffs) { trim(); }

Polynomial Polynomial::mono(double coef, int degree) {
    std::vector<double> v(degree + 1, 0.0);
    v[degree] = coef;
    return Polynomial(v);
}

Polynomial& Polynomial::trim() {
    while (!c.empty() && c.back() == 0.0) c.pop_back();
    return *this;
}

int Polynomial::degree() const { return c.empty() ? -1 : (int)c.size() - 1; }

double Polynomial::coeff(int k) const { return (k >= 0 && k < (int)c.size()) ? c[k] : 0.0; }

double Polynomial::eval(double x) const {
    // 霍纳法：从高次往低次叠加
    double r = 0;
    for (int i = degree(); i >= 0; i--) r = r * x + c[i];
    return r;
}

Polynomial& Polynomial::add(const Polynomial& b) {
    if ((int)c.size() < (int)b.c.size()) c.resize(b.c.size(), 0.0);
    for (int i = 0; i < (int)b.c.size(); i++) c[i] += b.c[i];
    return trim();
}

Polynomial& Polynomial::sub(const Polynomial& b) {
    if ((int)c.size() < (int)b.c.size()) c.resize(b.c.size(), 0.0);
    for (int i = 0; i < (int)b.c.size(); i++) c[i] -= b.c[i];
    return trim();
}

Polynomial Polynomial::mul(const Polynomial& b) const {
    if (c.empty() || b.c.empty()) return Polynomial();
    std::vector<double> r(c.size() + b.c.size() - 1, 0.0);
    for (int i = 0; i < (int)c.size(); i++)
        for (int j = 0; j < (int)b.c.size(); j++)
            r[i + j] += c[i] * b.c[j];
    return Polynomial(r);
}

Polynomial Polynomial::derivative() const {
    if (c.empty()) return Polynomial();
    std::vector<double> r;
    for (int i = 1; i < (int)c.size(); i++) r.push_back(c[i] * i);
    return Polynomial(r);
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
}

int polynomial_self_test() {
    g_fails = 0;
    {
        // p(x) = 2 + 3x + 4x^2
        std::vector<double> co;
        co.push_back(2); co.push_back(3); co.push_back(4);
        Polynomial p(co);
        expect("poly-eval", p.eval(2) == 24.0);
        expect("poly-degree", p.degree() == 2);
        expect("poly-coeff", p.coeff(1) == 3.0 && p.coeff(5) == 0.0);
    }
    {
        // (1+2x)+(3+4x) = 4+6x
        std::vector<double> a, b;
        a.push_back(1); a.push_back(2);
        b.push_back(3); b.push_back(4);
        Polynomial pa(a), pb(b);
        Polynomial s = pa; s.add(pb);
        expect("poly-add", s.eval(0) == 4 && s.eval(1) == 10);
        Polynomial d = pa; d.sub(pb);
        expect("poly-sub", d.eval(0) == -2 && d.eval(1) == -4);
    }
    {
        // (1+2x)(3+4x) = 3+10x+8x^2
        std::vector<double> a, b;
        a.push_back(1); a.push_back(2);
        b.push_back(3); b.push_back(4);
        Polynomial p = Polynomial(a).mul(Polynomial(b));
        expect("poly-mul", p.coeff(0) == 3 && p.coeff(1) == 10 && p.coeff(2) == 8);
        expect("poly-mul-eval", p.eval(1) == 21);
    }
    {
        // d/dx (2 + 3x + 4x^2) = 3 + 8x
        std::vector<double> co;
        co.push_back(2); co.push_back(3); co.push_back(4);
        Polynomial d = Polynomial(co).derivative();
        expect("poly-derivative", d.coeff(0) == 3 && d.coeff(1) == 8);
    }
    return g_fails;
}

} // namespace mathx
} // namespace nefu
