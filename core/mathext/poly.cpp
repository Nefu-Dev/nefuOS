#pragma GCC optimize("no-tree-loop-distribute-patterns")
// nefuOS 数学扩展库 —— 多项式模块实现
#include "poly.h"
#include "complex.h"   // d_close
#include <cmath>
#include <cstddef>

namespace nefu {
namespace mathext {

void poly_free(double* p) {
    if (p) delete[] p;
}

int poly_degree(const double* p, int n) {
    if (!p || n <= 0) return -1;
    int d = n - 1;
    while (d >= 0 && std::fabs(p[d]) < 1e-14) d--;
    return d;
}

// Horner 法：p = c[0]; for i=deg..1: p = p*x + c[i]。O(n)，数值稳定。
double poly_eval(const double* p, int n, double x) {
    if (!p || n <= 0) return 0.0;
    double y = p[n - 1];
    for (int i = n - 2; i >= 0; i--) y = y * x + p[i];
    return y;
}

// 去前导零，返回有效次数+1
static int poly_trim(double* p, int n) {
    while (n > 1 && std::fabs(p[n - 1]) < 1e-14) n--;
    return n;
}

double* poly_add(const double* a, int na, const double* b, int nb, int& out_n) {
    int n = na > nb ? na : nb;
    double* r = new double[(size_t)n];
    for (int i = 0; i < n; i++) {
        double av = (i < na) ? a[i] : 0.0;
        double bv = (i < nb) ? b[i] : 0.0;
        r[i] = av + bv;
    }
    out_n = poly_trim(r, n);
    return r;
}

double* poly_sub(const double* a, int na, const double* b, int nb, int& out_n) {
    int n = na > nb ? na : nb;
    double* r = new double[(size_t)n];
    for (int i = 0; i < n; i++) {
        double av = (i < na) ? a[i] : 0.0;
        double bv = (i < nb) ? b[i] : 0.0;
        r[i] = av - bv;
    }
    out_n = poly_trim(r, n);
    return r;
}

double* poly_mul(const double* a, int na, const double* b, int nb, int& out_n) {
    if (na <= 0 || nb <= 0) { out_n = 0; return 0; }
    int n = na + nb - 1;
    double* r = new double[(size_t)n];
    for (int i = 0; i < n; i++) r[i] = 0.0;
    for (int i = 0; i < na; i++)
        for (int j = 0; j < nb; j++)
            r[i + j] += a[i] * b[j];
    out_n = n;
    return r;
}

// 多项式长除法：a / b，返回商，余数出参。
double* poly_divmod(const double* a, int na, const double* b, int nb,
                    double*& out_rem, int& out_rem_n) {
    out_rem = 0; out_rem_n = 0;
    int da = poly_degree(a, na);
    int db = poly_degree(b, nb);
    if (db < 0) return 0;                 // 除零
    if (da < db) {                        // 商为 0，余数即 a
        double* rem = new double[(size_t)na];
        for (int i = 0; i < na; i++) rem[i] = a[i];
        out_rem = rem; out_rem_n = na;
        double* q = new double[1]; q[0] = 0.0;
        return q;
    }
    int qn = da - db + 1;
    double* q = new double[(size_t)qn];
    for (int i = 0; i < qn; i++) q[i] = 0.0;
    // 余数初始化为 a 的拷贝
    double* rem = new double[(size_t)(da + 1)];
    for (int i = 0; i <= da; i++) rem[i] = a[i];
    double lead = b[db];
    for (int k = qn - 1; k >= 0; k--) {
        double factor = rem[db + k] / lead;
        q[k] = factor;
        for (int j = db + k; j >= k; j--) rem[j] -= factor * b[j - k];
    }
    out_rem = rem;
    out_rem_n = poly_trim(rem, da + 1);
    return q;
}

// p(q(x))：q 的多项式代入 p。p 有 np 项，p(x)=sum p[i] x^i。
double* poly_compose(const double* p, int np, const double* q, int nq, int& out_n) {
    out_n = 0;
    // Horner 方式：result = 0; for i=np-1..0: result = result*q + p[i]
    double* result = new double[1];
    result[0] = 0.0;
    int rn = 1;
    for (int i = np - 1; i >= 0; i--) {
        // result = result * q
        int mn;
        double* t = poly_mul(result, rn, q, nq, mn);
        delete[] result;
        // result += p[i]
        double c[1] = {p[i]};
        int sn;
        double* s = poly_add(t, mn, c, 1, sn);
        delete[] t;
        result = s;
        rn = sn;
    }
    out_n = rn;
    return result;
}

double* poly_derivative(const double* p, int n, int& out_n) {
    if (n <= 1) { out_n = 1; double* r = new double[1]; r[0] = 0.0; return r; }
    double* r = new double[(size_t)(n - 1)];
    for (int i = 1; i < n; i++) r[i - 1] = p[i] * (double)i;
    out_n = n - 1;
    return r;
}

double* poly_integral(const double* p, int n, int& out_n) {
    double* r = new double[(size_t)(n + 1)];
    r[0] = 0.0;   // 积分常数取 0
    for (int i = 0; i < n; i++) r[i + 1] = p[i] / (double)(i + 1);
    out_n = n + 1;
    return r;
}

// 多项式 GCD：Euclid 算法，每次用余数替换，最后首一化。
double* poly_gcd(const double* a, int na, const double* b, int nb, int& out_n) {
    // 规范化：首一化（保证输入首一，避免系数膨胀）
    double* x = new double[(size_t)na];
    for (int i = 0; i < na; i++) x[i] = a[i];
    int dx = poly_degree(x, na);
    if (dx >= 0) {
        double lead = x[dx];
        if (std::fabs(lead) > 1e-14)
            for (int i = 0; i <= dx; i++) x[i] /= lead;
    }
    int xn = na;
    double* y = new double[(size_t)nb];
    for (int i = 0; i < nb; i++) y[i] = b[i];
    int dy = poly_degree(y, nb);
    if (dy >= 0) {
        double lead = y[dy];
        if (std::fabs(lead) > 1e-14)
            for (int i = 0; i <= dy; i++) y[i] /= lead;
    }
    int yn = nb;
    while (poly_degree(y, yn) >= 0) {
        double* rem; int rn;
        double* q = poly_divmod(x, xn, y, yn, rem, rn);
        delete[] q;
        delete[] x;
        x = y; xn = yn;
        y = rem; yn = rn;
    }
    // 首一化
    int d = poly_degree(x, xn);
    if (d < 0) { out_n = 1; double* r = new double[1]; r[0] = 1.0; delete[] x; return r; }
    double lead = x[d];
    if (std::fabs(lead) > 1e-14)
        for (int i = 0; i <= d; i++) x[i] /= lead;
    out_n = d + 1;
    return x;
}

// ---------------- 根求解 ----------------
// 在 [lo,hi] 内以步长扫描符号变化，用二分法收紧到 eps。
int poly_roots(const double* p, int n, double lo, double hi,
               double* out_roots, int max_out, double eps) {
    int found = 0;
    const int STEPS = 2000;                 // 扫描网格密度
    double step = (hi - lo) / (double)STEPS;
    double prev_x = lo;
    double prev_y = poly_eval(p, n, prev_x);
    for (int s = 1; s <= STEPS && found < max_out; s++) {
        double x = lo + step * s;
        double y = poly_eval(p, n, x);
        if (std::fabs(y) < eps) {           // 恰好落在根上
            out_roots[found++] = x;
        } else if (prev_y * y < 0.0) {      // 符号变化 -> 二分
            double a = prev_x, b = x;
            double fa = prev_y, fb = y;
            for (int it = 0; it < 100; it++) {
                double m = 0.5 * (a + b);
                double fm = poly_eval(p, n, m);
                if (std::fabs(fm) < eps) { a = m; break; }
                if (fa * fm < 0.0) { b = m; fb = fm; }
                else { a = m; fa = fm; }
            }
            out_roots[found++] = 0.5 * (a + b);
        }
        prev_x = x; prev_y = y;
    }
    return found;
}

// Taylor：q(t) = p(a+t)。用反复导数/Taylor 公式：
// q[k] = p^(k)(a) / k!。对 p 逐次求导并在 a 处求值。
double* poly_taylor(const double* p, int n, double a, int& out_n) {
    int deg = poly_degree(p, n);
    if (deg < 0) { out_n = 1; double* r = new double[1]; r[0] = 0.0; return r; }
    double* q = new double[(size_t)(deg + 1)];
    double* cur = new double[(size_t)n];
    for (int i = 0; i < n; i++) cur[i] = p[i];
    int cn = n;
    double fact = 1.0;
    for (int k = 0; k <= deg; k++) {
        q[k] = poly_eval(cur, cn, a) / fact;
        // cur = cur'
        int dn;
        double* d = poly_derivative(cur, cn, dn);
        delete[] cur;
        cur = d; cn = dn;
        fact *= (double)(k + 1);
    }
    delete[] cur;
    out_n = deg + 1;
    return q;
}

// 前向声明（poly_scale 本地封装）
static double* poly_scale_local(const double* p, int n, double s, int& out_n);

// ---------------- Lagrange 插值 ----------------
// L_i(x) = prod_{j!=i} (x-xj)/(xi-xj)，再求和 sum yi Li。
double* poly_lagrange(const double* xi, const double* yi, int n, int& out_n) {
    // 结果多项式初始化为 0
    double* result = new double[1];
    result[0] = 0.0;
    int rn = 1;
    for (int i = 0; i < n; i++) {
        // 基多项式 Li：初始 1/(xi-xj 乘积)，乘 prod (x-xj)
        double denom = 1.0;
        double* num = new double[1];
        num[0] = 1.0;
        int nn = 1;
        for (int j = 0; j < n; j++) {
            if (j == i) continue;
            denom *= (xi[i] - xi[j]);
            // num *= (x - xj)
            double lin[2] = { -xi[j], 1.0 };
            int mn;
            double* t = poly_mul(num, nn, lin, 2, mn);
            delete[] num;
            num = t; nn = mn;
        }
        double scale = yi[i] / denom;
        // result += scale * num
        int sn;
        double* scaled = poly_scale_local(num, nn, scale, sn);
        delete[] num;
        int an;
        double* s = poly_add(result, rn, scaled, sn, an);
        delete[] scaled;
        delete[] result;
        result = s; rn = an;
    }
    out_n = rn;
    return result;
}

// 多项式数乘的小封装（poly_scale 尚未在头文件声明，这里本地实现）
static double* poly_scale_local(const double* p, int n, double s, int& out_n) {
    double* r = new double[(size_t)(n > 0 ? n : 1)];
    for (int i = 0; i < n; i++) r[i] = p[i] * s;
    out_n = n;
    return r;
}

// ---------------- Newton 插值（差商表） ----------------
double* poly_newton(const double* xi, const double* yi, int n, int& out_n) {
    // coef[i] = f[x0..xi]
    double* coef = new double[(size_t)n];
    for (int i = 0; i < n; i++) coef[i] = yi[i];
    for (int j = 1; j < n; j++) {
        for (int i = n - 1; i >= j; i--) {
            coef[i] = (coef[i] - coef[i - 1]) / (xi[i] - xi[i - j]);
        }
    }
    out_n = n;
    return coef;
}

double poly_newton_eval(const double* xi, const double* coef, int n, double x) {
    // 嵌套求值：p = coef[n-1]; for i=n-2..0: p = p*(x-xi[i]) + coef[i]
    double p = coef[n - 1];
    for (int i = n - 2; i >= 0; i--) p = p * (x - xi[i]) + coef[i];
    return p;
}
// ---------------- 自检 ----------------
int poly_self_test() {
    int fails = 0;
    const double EPS = 1e-8;

    // 1. p(x)=x^2-3x+2：系数 [2,-3,1]
    double p[3] = {2, -3, 1};
    if (!d_close(poly_eval(p, 3, 2.0), 0.0, EPS)) fails++;
    if (!d_close(poly_eval(p, 3, 1.0), 0.0, EPS)) fails++;
    if (!d_close(poly_eval(p, 3, 0.0), 2.0, EPS)) fails++;

    // 2. 乘法：(x-1)*(x-2) = x^2-3x+2
    double f1[2] = {-1, 1};   // x-1
    double f2[2] = {-2, 1};   // x-2
    int mn;
    double* prod = poly_mul(f1, 2, f2, 2, mn);
    if (mn != 3) fails++;
    else {
        if (!d_close(prod[0], 2.0, EPS)) fails++;
        if (!d_close(prod[1], -3.0, EPS)) fails++;
        if (!d_close(prod[2], 1.0, EPS)) fails++;
    }
    delete[] prod;

    // 3. 除法：(x^2-3x+2)/(x-1) = x-2，余数 0
    double* rem; int rn;
    double* q = poly_divmod(p, 3, f1, 2, rem, rn);
    if (!d_close(q[0], -2.0, EPS)) fails++;
    if (!d_close(q[1], 1.0, EPS)) fails++;
    if (poly_degree(rem, rn) >= 0) fails++;   // 余数应为零
    delete[] q; delete[] rem;

    // 4. 导数：p'(x) = 2x-3，系数 [-3,2]
    int dn;
    double* d = poly_derivative(p, 3, dn);
    if (dn != 2) fails++;
    else {
        if (!d_close(d[0], -3.0, EPS)) fails++;
        if (!d_close(d[1], 2.0, EPS)) fails++;
    }
    delete[] d;

    // 5. GCD：gcd(x^2-3x+2, x-1) = x-1
    int gn;
    double* g = poly_gcd(p, 3, f1, 2, gn);
    if (gn != 2) fails++;
    else {
        if (!d_close(g[0], -1.0, 1e-6)) fails++;   // 首一化后 x-1
        if (!d_close(g[1], 1.0, 1e-6)) fails++;
    }
    delete[] g;

    // 6. 根：p(x)=x^2-3x+2 的根为 1 和 2
    double roots[8];
    int rc = poly_roots(p, 3, -10, 10, roots, 8);
    if (rc < 2) fails++;
    else {
        // 两个根应接近 1 和 2
        bool found1 = false, found2 = false;
        for (int i = 0; i < rc; i++) {
            if (d_close(roots[i], 1.0, 1e-6)) found1 = true;
            if (d_close(roots[i], 2.0, 1e-6)) found2 = true;
        }
        if (!found1 || !found2) fails++;
    }

    // 7. Taylor：p(x)=(x-1)^2 = x^2-2x+1 在 a=1 处展开 = t^2
    double p2[3] = {1, -2, 1};
    int tn;
    double* t = poly_taylor(p2, 3, 1.0, tn);
    if (tn != 3) fails++;
    else {
        if (!d_close(t[0], 0.0, EPS)) fails++;
        if (!d_close(t[1], 0.0, EPS)) fails++;
        if (!d_close(t[2], 1.0, EPS)) fails++;
    }
    delete[] t;

    return fails;
}

} // namespace mathext
} // namespace nefu
