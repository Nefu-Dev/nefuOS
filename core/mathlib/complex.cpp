// nefuOS mathlib —— 复数实现 + 自测
#include "mathlib/complex.h"
#include <cstdio>

namespace nefu {
namespace mathx {

Complex& Complex::add(const Complex& b) { re += b.re; im += b.im; return *this; }
Complex& Complex::sub(const Complex& b) { re -= b.re; im -= b.im; return *this; }
Complex& Complex::mul(const Complex& b) {
    double nr = re * b.re - im * b.im;
    double ni = re * b.im + im * b.re;
    re = nr; im = ni;
    return *this;
}
Complex& Complex::div(const Complex& b) {
    double d2 = b.re * b.re + b.im * b.im;
    if (d2 == 0) { re = 0; im = 0; return *this; }
    double nr = (re * b.re + im * b.im) / d2;
    double ni = (im * b.re - re * b.im) / d2;
    re = nr; im = ni;
    return *this;
}

bool Complex::near(const Complex& b, double eps) const {
    return (re - b.re) * (re - b.re) + (im - b.im) * (im - b.im) < eps * eps;
}

Complex Complex::from_polar(double r, double theta) {
    return Complex(r * std::cos(theta), r * std::sin(theta));
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
}

int complex_self_test() {
    g_fails = 0;
    {
        Complex a(1, 2), b(3, 4);
        Complex s = a + b;
        expect("complex-add", s.near(Complex(4, 6)));
        Complex m = a * b;
        expect("complex-mul", m.near(Complex(-5, 10)));
        Complex q = m / b;
        expect("complex-div", q.near(a));
    }
    {
        Complex i(0, 1);
        Complex ii = i * i;
        expect("complex-ii", ii.near(Complex(-1)));
        expect("complex-mag", i.mag() == 1.0);
        Complex c(1, 1);
        expect("complex-arg", (c.arg() - 3.14159265358979 / 4) < 1e-9);
        expect("complex-conj", c.conj().near(Complex(1, -1)));
    }
    {
        Complex z = Complex::from_polar(2.0, 0.0);
        expect("complex-polar", z.near(Complex(2, 0), 1e-9));
        Complex w = Complex::from_polar(1.0, 3.14159265358979 / 2);
        expect("complex-polar90", w.near(Complex(0, 1), 1e-9));
        expect("complex-near", Complex(1, 1).near(Complex(1, 1)));
    }
    return g_fails;
}

} // namespace mathx
} // namespace nefu
