// nefuOS numeric algorithm library
// (1) Complex arithmetic and a recursive Cooley-Tukey FFT;
// (2) dense Matrix ops: multiply / transpose / trace / determinant /
//     inverse via Gaussian elimination (doubles, small sizes);
// (3) descriptive statistics: min/max/mean/variance/std/median/percentile;
// (4) numerical integration: trapezoid and Simpson rules;
// (5) number theory helpers: gcd family, extended Euclid, modular inverse,
//     Chinese remainder theorem, fast power;
// (6) fast deterministic PRNGs: LCG, xorshift32, splitmix64.
// Doubles are used here (host and apps side); no STL.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <math.h>

namespace nefu {
namespace num {

// =====================================================================
// Complex
// =====================================================================
struct Complex {
    double re, im;
    Complex() : re(0), im(0) {}
    Complex(double r, double i = 0) : re(r), im(i) {}
    Complex operator+(const Complex& o) const { return Complex(re + o.re, im + o.im); }
    Complex operator-(const Complex& o) const { return Complex(re - o.re, im - o.im); }
    Complex operator*(const Complex& o) const {
        return Complex(re * o.re - im * o.im, re * o.im + im * o.re);
    }
    Complex operator/(const Complex& o) const {
        double d = o.re * o.re + o.im * o.im;
        if (d == 0) return Complex(0, 0);
        return Complex((re * o.re + im * o.im) / d, (im * o.re - re * o.im) / d);
    }
    Complex operator-() const { return Complex(-re, -im); }
    double mag() const { return sqrt(re * re + im * im); }
    double arg() const { return atan2(im, re); }
    Complex conj() const { return Complex(re, -im); }
};
Complex c_polar(double r, double theta);      // r * (cos + i sin)

// =====================================================================
// FFT — recursive radix-2 Cooley-Tukey.  n must be a power of two.
// `invert` runs the inverse transform (scaled by 1/n).
// =====================================================================
void fft(Complex* a, int n, bool invert);

// =====================================================================
// Matrix — fixed max size 16x16, row-major double storage
// =====================================================================
class Matrix {
public:
    static const int MAXN = 16;
    Matrix() : rows_(0), cols_(0) {}
    Matrix(int r, int c) { resize(r, c); }
    void resize(int r, int c);              // zero-fill
    int  rows() const { return rows_; }
    int  cols() const { return cols_; }
    double& at(int r, int c) { return m_[r][c]; }
    double  at(int r, int c) const { return m_[r][c]; }
    // ops (all sizes must match; zero-size results on error)
    Matrix mul(const Matrix& o) const;
    Matrix transpose() const;
    double trace() const;
    double determinant() const;             // Gaussian elimination
    bool   inverse(Matrix* out) const;      // returns false if singular
    bool   solve(const double* b, double* x) const;   // A x = b (square)
    static Matrix identity(int n);
private:
    int rows_, cols_;
    double m_[MAXN][MAXN];
    bool gauss(double* aug, int n) const;   // helper: reduced row echelon
};

// =====================================================================
// descriptive statistics (plain double arrays)
// =====================================================================
double stat_min(const double* a, int n);
double stat_max(const double* a, int n);
double stat_mean(const double* a, int n);
double stat_var(const double* a, int n);            // sample variance (n-1)
double stat_std(const double* a, int n);
double stat_median(const double* a, int n);         // sorts a copy; even n -> mean of middles
double stat_percentile(const double* a, int n, double p); // p in [0,100]
double stat_cov(const double* a, const double* b, int n);  // sample covariance

// =====================================================================
// numerical integration on [a, b] with n+1 samples
// =====================================================================
double integrate_trap(double (*f)(double), double a, double b, int n);
double integrate_simpson(double (*f)(double), double a, double b, int n);  // n even

// =====================================================================
// number theory
// =====================================================================
int64_t gcd64(int64_t a, int64_t b);
int64_t lcm64(int64_t a, int64_t b);
int64_t egcd(int64_t a, int64_t b, int64_t* x, int64_t* y);   // ax+by=gcd
int64_t mod_inv(int64_t a, int64_t m);                        // inverse or -1
int64_t mod_pow(int64_t base, int64_t exp, int64_t mod);      // fast power
int64_t crt_pair(int64_t r1, int64_t m1, int64_t r2, int64_t m2, bool* ok);
// find x s.t. x == r_i (mod m_i), pairwise coprime moduli
int64_t crt(const int64_t* r, const int64_t* m, int k, bool* ok);

// =====================================================================
// PRNGs (deterministic, thread-local-free)
// =====================================================================
struct LCG {
    uint32_t s;
    explicit LCG(uint32_t seed) : s(seed) {}
    uint32_t next();                        // 0..2^32-1
    int next_int(int lo, int hi);           // inclusive
};
struct Xorshift32 {
    uint32_t s;
    explicit Xorshift32(uint32_t seed) : s(seed ? seed : 1) {}
    uint32_t next();
    int next_int(int lo, int hi);
};
struct SplitMix64 {
    uint64_t s;
    explicit SplitMix64(uint64_t seed) : s(seed) {}
    uint64_t next();
    int next_int(int lo, int hi);           // maps high bits
};

// =====================================================================
// self test
// =====================================================================
int numeric_self_test();

} // namespace num
} // namespace nefu
