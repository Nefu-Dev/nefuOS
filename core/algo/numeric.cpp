// nefuOS numeric algorithm library — implementation & self test
// See numeric.h for the API contract.
#include "numeric.h"
#include <string.h>
#include <math.h>

namespace nefu {
namespace num {

// =====================================================================
// Complex helpers
// =====================================================================
Complex c_polar(double r, double theta) {
    return Complex(r * cos(theta), r * sin(theta));
}

// =====================================================================
// FFT — recursive radix-2 Cooley-Tukey
// =====================================================================
static void fft_rec(Complex* a, int n, bool invert) {
    if (n <= 1) return;
    // split into even and odd indexed halves (bit-reversal via recursion)
    Complex* even = new Complex[(size_t)(n / 2)];
    Complex* odd  = new Complex[(size_t)(n / 2)];
    for (int i = 0; i < n / 2; i++) {
        even[i] = a[2 * i];
        odd[i]  = a[2 * i + 1];
    }
    fft_rec(even, n / 2, invert);
    fft_rec(odd,  n / 2, invert);
    // combine: a[k] = even[k] + w^k * odd[k], a[k+n/2] = even[k] - w^k * odd[k]
    double ang = 2.0 * 3.14159265358979323846 / (double)n * (invert ? -1.0 : 1.0);
    Complex w(1.0, 0.0);
    Complex wn(cos(ang), sin(ang));
    for (int k = 0; k < n / 2; k++) {
        Complex t = w * odd[k];
        a[k]         = even[k] + t;
        a[k + n / 2] = even[k] - t;
        w = w * wn;
    }
    delete[] even;
    delete[] odd;
}

void fft(Complex* a, int n, bool invert) {
    if (n <= 1) return;
    fft_rec(a, n, invert);
    // scale exactly once, at the top level
    if (invert)
        for (int i = 0; i < n; i++) { a[i].re /= (double)n; a[i].im /= (double)n; }
}

// =====================================================================
// Matrix
// =====================================================================
void Matrix::resize(int r, int c) {
    rows_ = (r > 0 && r <= MAXN) ? r : 0;
    cols_ = (c > 0 && c <= MAXN) ? c : 0;
    for (int i = 0; i < MAXN; i++)
        for (int j = 0; j < MAXN; j++) m_[i][j] = 0;
}

Matrix Matrix::mul(const Matrix& o) const {
    Matrix out(rows_, o.cols_);
    if (cols_ != o.rows_) return out;          // mismatched: zero matrix
    for (int i = 0; i < rows_; i++)
        for (int j = 0; j < o.cols_; j++) {
            double s = 0;
            for (int k = 0; k < cols_; k++) s += m_[i][k] * o.m_[k][j];
            out.m_[i][j] = s;
        }
    return out;
}

Matrix Matrix::transpose() const {
    Matrix out(cols_, rows_);
    for (int i = 0; i < rows_; i++)
        for (int j = 0; j < cols_; j++) out.m_[j][i] = m_[i][j];
    return out;
}

double Matrix::trace() const {
    double s = 0;
    int n = rows_ < cols_ ? rows_ : cols_;
    for (int i = 0; i < n; i++) s += m_[i][i];
    return s;
}

// ---------------------------------------------------------------------
// Gaussian elimination helper: aug is (n x (n+1)) row-major; performs
// forward elimination + back substitution.  Returns false if singular.
// ---------------------------------------------------------------------
bool Matrix::gauss(double* aug, int n) const {
    for (int col = 0; col < n; col++) {
        // find the pivot row with the largest absolute value
        int piv = col;
        double best = fabs(aug[col * (n + 1) + col]);
        for (int r = col + 1; r < n; r++) {
            double v = fabs(aug[r * (n + 1) + col]);
            if (v > best) { best = v; piv = r; }
        }
        if (best < 1e-12) return false;        // singular
        if (piv != col)
            for (int j = col; j <= n; j++) {
                double t = aug[col * (n + 1) + j];
                aug[col * (n + 1) + j] = aug[piv * (n + 1) + j];
                aug[piv * (n + 1) + j] = t;
            }
        // eliminate below
        double d = aug[col * (n + 1) + col];
        for (int r = col + 1; r < n; r++) {
            double f = aug[r * (n + 1) + col] / d;
            for (int j = col; j <= n; j++)
                aug[r * (n + 1) + j] -= f * aug[col * (n + 1) + j];
        }
    }
    // back substitution
    for (int r = n - 1; r >= 0; r--) {
        double s = aug[r * (n + 1) + n];
        for (int j = r + 1; j < n; j++) s -= aug[r * (n + 1) + j] * aug[j * (n + 1) + n];
        aug[r * (n + 1) + n] = s / aug[r * (n + 1) + r];
    }
    return true;
}

double Matrix::determinant() const {
    if (rows_ != cols_) return 0;
    int n = rows_;
    // copy into an augmented scratch (identity column unused)
    double* a = new double[(size_t)n * (size_t)(n + 1)];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) a[i * (n + 1) + j] = m_[i][j];
    // LU-ish elimination tracking the sign of row swaps
    double det = 1;
    int sign = 1;
    for (int col = 0; col < n; col++) {
        int piv = col;
        double best = fabs(a[col * (n + 1) + col]);
        for (int r = col + 1; r < n; r++) {
            double v = fabs(a[r * (n + 1) + col]);
            if (v > best) { best = v; piv = r; }
        }
        if (best < 1e-12) { det = 0; break; }
        if (piv != col) {
            sign = -sign;
            for (int j = col; j < n; j++) {
                double t = a[col * (n + 1) + j];
                a[col * (n + 1) + j] = a[piv * (n + 1) + j];
                a[piv * (n + 1) + j] = t;
            }
        }
        double d = a[col * (n + 1) + col];
        det *= d;
        for (int r = col + 1; r < n; r++) {
            double f = a[r * (n + 1) + col] / d;
            for (int j = col; j < n; j++)
                a[r * (n + 1) + j] -= f * a[col * (n + 1) + j];
        }
    }
    delete[] a;
    return sign * det;
}

bool Matrix::inverse(Matrix* out) const {
    if (!out || rows_ != cols_) return false;
    int n = rows_;
    // Gauss-Jordan on an n x 2n augmented matrix [A | I]; the right half
    // becomes A^-1 when the left half is reduced to I.
    double* aug = new double[(size_t)n * (size_t)(2 * n)];
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) aug[i * (2 * n) + j] = m_[i][j];
        for (int j = 0; j < n; j++) aug[i * (2 * n) + n + j] = (i == j) ? 1 : 0;
    }
    for (int col = 0; col < n; col++) {
        int piv = col;
        double best = fabs(aug[col * (2 * n) + col]);
        for (int r = col + 1; r < n; r++) {
            double v = fabs(aug[r * (2 * n) + col]);
            if (v > best) { best = v; piv = r; }
        }
        if (best < 1e-12) { delete[] aug; return false; }
        if (piv != col)
            for (int j = 0; j < 2 * n; j++) {
                double t = aug[col * (2 * n) + j];
                aug[col * (2 * n) + j] = aug[piv * (2 * n) + j];
                aug[piv * (2 * n) + j] = t;
            }
        double d = aug[col * (2 * n) + col];
        for (int j = 0; j < 2 * n; j++) aug[col * (2 * n) + j] /= d;
        for (int r = 0; r < n; r++) {
            if (r == col) continue;
            double f = aug[r * (2 * n) + col];
            for (int j = 0; j < 2 * n; j++)
                aug[r * (2 * n) + j] -= f * aug[col * (2 * n) + j];
        }
    }
    out->resize(n, n);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) out->m_[i][j] = aug[i * (2 * n) + n + j];
    delete[] aug;
    return true;
}

bool Matrix::solve(const double* b, double* x) const {
    if (rows_ != cols_) return false;
    int n = rows_;
    double* aug = new double[(size_t)n * (size_t)(n + 1)];
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) aug[i * (n + 1) + j] = m_[i][j];
        aug[i * (n + 1) + n] = b[i];
    }
    bool ok = gauss(aug, n);
    if (ok) for (int i = 0; i < n; i++) x[i] = aug[i * (n + 1) + n];
    delete[] aug;
    return ok;
}

Matrix Matrix::identity(int n) {
    Matrix out(n, n);
    for (int i = 0; i < n; i++) out.m_[i][i] = 1;
    return out;
}

// =====================================================================
// statistics
// =====================================================================
double stat_min(const double* a, int n) {
    if (n <= 0) return 0;
    double v = a[0];
    for (int i = 1; i < n; i++) if (a[i] < v) v = a[i];
    return v;
}

double stat_max(const double* a, int n) {
    if (n <= 0) return 0;
    double v = a[0];
    for (int i = 1; i < n; i++) if (a[i] > v) v = a[i];
    return v;
}

double stat_mean(const double* a, int n) {
    if (n <= 0) return 0;
    double s = 0;
    for (int i = 0; i < n; i++) s += a[i];
    return s / n;
}

double stat_var(const double* a, int n) {
    if (n < 2) return 0;
    double m = stat_mean(a, n), s = 0;
    for (int i = 0; i < n; i++) { double d = a[i] - m; s += d * d; }
    return s / (n - 1);
}

double stat_std(const double* a, int n) { return sqrt(stat_var(a, n)); }

double stat_median(const double* a, int n) {
    if (n <= 0) return 0;
    double* copy = new double[(size_t)n];
    for (int i = 0; i < n; i++) copy[i] = a[i];
    // simple insertion sort (n is small in practice)
    for (int i = 1; i < n; i++) {
        double k = copy[i];
        int j = i - 1;
        while (j >= 0 && copy[j] > k) { copy[j + 1] = copy[j]; j--; }
        copy[j + 1] = k;
    }
    double v = (n % 2) ? copy[n / 2] : (copy[n / 2 - 1] + copy[n / 2]) / 2;
    delete[] copy;
    return v;
}

double stat_percentile(const double* a, int n, double p) {
    if (n <= 0) return 0;
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    double* copy = new double[(size_t)n];
    for (int i = 0; i < n; i++) copy[i] = a[i];
    for (int i = 1; i < n; i++) {
        double k = copy[i];
        int j = i - 1;
        while (j >= 0 && copy[j] > k) { copy[j + 1] = copy[j]; j--; }
        copy[j + 1] = k;
    }
    double pos = p / 100.0 * (n - 1);
    int lo = (int)pos;
    int hi = lo + 1 < n ? lo + 1 : lo;
    double frac = pos - lo;
    double v = copy[lo] + frac * (copy[hi] - copy[lo]);
    delete[] copy;
    return v;
}

double stat_cov(const double* a, const double* b, int n) {
    if (n < 2) return 0;
    double ma = stat_mean(a, n), mb = stat_mean(b, n), s = 0;
    for (int i = 0; i < n; i++) s += (a[i] - ma) * (b[i] - mb);
    return s / (n - 1);
}

// =====================================================================
// numerical integration
// =====================================================================
double integrate_trap(double (*f)(double), double a, double b, int n) {
    if (n <= 0) return 0;
    double h = (b - a) / n;
    double s = 0.5 * (f(a) + f(b));
    for (int i = 1; i < n; i++) s += f(a + i * h);
    return s * h;
}

double integrate_simpson(double (*f)(double), double a, double b, int n) {
    if (n <= 0) return 0;
    if (n % 2) n++;                      // Simpson needs an even panel count
    double h = (b - a) / n;
    double s = f(a) + f(b);
    for (int i = 1; i < n; i++) {
        double x = a + i * h;
        s += (i % 2) ? 4 * f(x) : 2 * f(x);
    }
    return s * h / 3;
}

// =====================================================================
// number theory
// =====================================================================
int64_t gcd64(int64_t a, int64_t b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) { int64_t t = a % b; a = b; b = t; }
    return a;
}

int64_t lcm64(int64_t a, int64_t b) {
    if (!a || !b) return 0;
    int64_t g = gcd64(a, b);
    return a / g * b;
}

int64_t egcd(int64_t a, int64_t b, int64_t* x, int64_t* y) {
    if (b == 0) { *x = 1; *y = 0; return a; }
    int64_t x1, y1;
    int64_t g = egcd(b, a % b, &x1, &y1);
    *x = y1;
    *y = x1 - (a / b) * y1;
    return g;
}

int64_t mod_inv(int64_t a, int64_t m) {
    int64_t x, y;
    int64_t g = egcd(a, m, &x, &y);
    if (g != 1) return -1;
    int64_t r = x % m;
    if (r < 0) r += m;
    return r;
}

int64_t mod_pow(int64_t base, int64_t exp, int64_t mod) {
    if (mod <= 0) return 0;
    base %= mod;
    if (base < 0) base += mod;
    int64_t r = 1;
    while (exp > 0) {
        if (exp & 1) r = (r * base) % mod;
        base = (base * base) % mod;
        exp >>= 1;
    }
    return r;
}

int64_t crt_pair(int64_t r1, int64_t m1, int64_t r2, int64_t m2, bool* ok) {
    // merge two congruences into one using the extended Euclid identity
    int64_t x, y;
    int64_t g = egcd(m1, m2, &x, &y);
    if ((r2 - r1) % g != 0) { if (ok) *ok = false; return 0; }
    int64_t l = lcm64(m1, m2);
    // x = r1 + m1 * ((r2-r1)/g * x mod (m2/g))
    int64_t k = (r2 - r1) / g;
    int64_t t = (k * (x % (m2 / g))) % (m2 / g);
    if (t < 0) t += m2 / g;
    int64_t r = (r1 + m1 * t) % l;
    if (r < 0) r += l;
    if (ok) *ok = true;
    return r;
}

int64_t crt(const int64_t* r, const int64_t* m, int k, bool* ok) {
    if (k <= 0) { if (ok) *ok = false; return 0; }
    int64_t R = r[0] % m[0], M = m[0];
    bool good = true;
    for (int i = 1; i < k && good; i++) {
        R = crt_pair(R, M, r[i] % m[i], m[i], &good);
        if (good) M = lcm64(M, m[i]);   // merge the moduli too
    }
    if (ok) *ok = good;
    return R;
}

// =====================================================================
// PRNGs
// =====================================================================
uint32_t LCG::next() {
    // Numerical Recipes constants
    s = s * 1664525u + 1013904223u;
    return s;
}

int LCG::next_int(int lo, int hi) {
    if (hi <= lo) return lo;
    uint32_t range = (uint32_t)(hi - lo + 1);
    return lo + (int)(next() % range);
}

uint32_t Xorshift32::next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

int Xorshift32::next_int(int lo, int hi) {
    if (hi <= lo) return lo;
    uint32_t range = (uint32_t)(hi - lo + 1);
    return lo + (int)(next() % range);
}

uint64_t SplitMix64::next() {
    s += 0x9E3779B97F4A7C15ull;
    uint64_t z = s;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

int SplitMix64::next_int(int lo, int hi) {
    if (hi <= lo) return lo;
    uint64_t r = next();
    uint32_t range = (uint32_t)(hi - lo + 1);
    return lo + (int)(r % range);
}

// =====================================================================
// self test
// =====================================================================
namespace {
int g_num_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) g_num_fails++;
    (void)what;
}
bool near(double a, double b, double eps = 1e-6) {
    double d = a - b;
    return d < eps && d > -eps;
}
} // namespace

int numeric_self_test() {
    g_num_fails = 0;

    // ---- complex ----
    {
        Complex a(3, 4), b(1, -2);
        expect("c-add", near((a + b).re, 4) && near((a + b).im, 2));
        expect("c-mul", near((a * b).re, 11) && near((a * b).im, -2));
        Complex q = a / b;
        expect("c-div", near(q.re, -1) && near(q.im, 2));
        expect("c-mag", near(a.mag(), 5));
        expect("c-arg", near(a.arg(), atan2(4.0, 3.0)));
        Complex p = c_polar(2, 3.14159265358979);
        expect("c-polar", near(p.re, -2, 1e-6) && near(p.im, 0, 1e-6));
    }
    // ---- FFT: impulse -> constant spectrum, then inverse recovers ----
    {
        const int N = 8;
        Complex x[N], orig[N];
        for (int i = 0; i < N; i++) { x[i] = Complex(i == 0 ? 1 : 0, 0); orig[i] = x[i]; }
        fft(x, N, false);
        expect("fft-dc", near(x[0].re, 1));
        expect("fft-flat", near(x[3].re, 1) && near(x[3].im, 0));
        fft(x, N, true);
        bool ok = true;
        for (int i = 0; i < N; i++)
            if (!near(x[i].re, orig[i].re) || !near(x[i].im, orig[i].im)) ok = false;
        expect("fft-roundtrip", ok);
    }
    // ---- matrix ----
    {
        Matrix a(2, 2), b(2, 2);
        a.at(0, 0) = 1; a.at(0, 1) = 2;
        a.at(1, 0) = 3; a.at(1, 1) = 4;
        b.at(0, 0) = 5; b.at(0, 1) = 6;
        b.at(1, 0) = 7; b.at(1, 1) = 8;
        Matrix c = a.mul(b);
        expect("m-mul", near(c.at(0, 0), 19) && near(c.at(0, 1), 22) &&
                        near(c.at(1, 0), 43) && near(c.at(1, 1), 50));
        expect("m-det", near(a.determinant(), -2));
        expect("m-trace", near(a.trace(), 5));
        Matrix t = a.transpose();
        expect("m-transpose", near(t.at(1, 0), 2));
        Matrix inv;
        expect("m-inv", a.inverse(&inv));
        Matrix chk = a.mul(inv);
        expect("m-inv-ok", near(chk.at(0, 0), 1) && near(chk.at(1, 1), 1) &&
                           near(chk.at(0, 1), 0));
        // solve: 2x + 3y = 8, x - y = -1  -> x=1, y=2
        Matrix s(2, 2);
        s.at(0, 0) = 2; s.at(0, 1) = 3;
        s.at(1, 0) = 1; s.at(1, 1) = -1;
        double bb[2] = {8, -1}, xx[2];
        expect("m-solve", s.solve(bb, xx) && near(xx[0], 1) && near(xx[1], 2));
        // singular matrix inverse must fail
        Matrix sing(2, 2);
        sing.at(0, 0) = 1; sing.at(0, 1) = 2;
        sing.at(1, 0) = 2; sing.at(1, 1) = 4;
        expect("m-singular", !sing.inverse(&inv));
        expect("m-det-zero", near(sing.determinant(), 0));
    }
    // ---- statistics ----
    {
        double a[5] = {1, 2, 3, 4, 5};
        expect("s-min", near(stat_min(a, 5), 1));
        expect("s-max", near(stat_max(a, 5), 5));
        expect("s-mean", near(stat_mean(a, 5), 3));
        expect("s-var", near(stat_var(a, 5), 2.5));      // sample variance
        expect("s-std", near(stat_std(a, 5), sqrt(2.5)));
        expect("s-median", near(stat_median(a, 5), 3));
        expect("s-p50", near(stat_percentile(a, 5, 50), 3));
        expect("s-p25", near(stat_percentile(a, 5, 25), 2));
        double b[4] = {10, 20, 30, 40};
        expect("s-median-even", near(stat_median(b, 4), 25));
        // covariance of x and 2x
        double c[4] = {1, 2, 3, 4}, d[4] = {2, 4, 6, 8};
        expect("s-cov", near(stat_cov(c, d, 4), 2 * stat_var(c, 4)));
    }
    // ---- integration ----
    {
        // integral of x^2 on [0,1] = 1/3
        struct F { static double x2(double x) { return x * x; } };
        expect("i-trap", near(integrate_trap(F::x2, 0, 1, 1000), 1.0 / 3, 1e-5));
        expect("i-simp", near(integrate_simpson(F::x2, 0, 1, 100), 1.0 / 3, 1e-8));
        // integral of sin on [0, pi] = 2
        expect("i-sin", near(integrate_simpson(sin, 0, 3.14159265358979, 100), 2, 1e-6));
    }
    // ---- number theory ----
    {
        expect("n-gcd", gcd64(48, 36) == 12);
        expect("n-lcm", lcm64(4, 6) == 12);
        int64_t x, y;
        expect("n-egcd", egcd(30, 12, &x, &y) == 6);
        expect("n-inv", mod_inv(3, 11) == 4);          // 3*4=12=1 mod 11
        expect("n-inv-fail", mod_inv(6, 9) == -1);
        expect("n-pow", mod_pow(2, 10, 1000) == 24);
        expect("n-pow-big", mod_pow(7, 100, 13) == 9);
        // CRT: x=2 mod 3, x=3 mod 5, x=2 mod 7 -> 23
        int64_t rs[3] = {2, 3, 2}, ms[3] = {3, 5, 7};
        bool ok = false;
        expect("n-crt", crt(rs, ms, 3, &ok) == 23 && ok);
        // inconsistent pair
        int64_t r2[2] = {1, 2}, m2[2] = {2, 4};
        ok = true;
        crt(r2, m2, 2, &ok);
        expect("n-crt-bad", !ok);
    }
    // ---- PRNG determinism ----
    {
        LCG a(123), b(123);
        expect("rng-lcg", a.next() == b.next() && a.next() == b.next());
        Xorshift32 x1(7), x2(7);
        expect("rng-xs", x1.next() == x2.next());
        SplitMix64 s1(99), s2(99);
        expect("rng-sm", s1.next() == s2.next());
        LCG c(42);
        int lo = 100, hi = 200;
        bool in_range = true;
        for (int i = 0; i < 1000; i++) {
            int v = c.next_int(lo, hi);
            if (v < lo || v > hi) in_range = false;
        }
        expect("rng-range", in_range);
    }
    return g_num_fails;
}

} // namespace num
} // namespace nefu
