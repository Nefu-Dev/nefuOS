#pragma GCC optimize("no-tree-loop-distribute-patterns")
// nefuOS 数学扩展库 —— 统计与随机数模块实现
#include "statistics.h"
#include "matrix.h"    // polyreg 用最小二乘法解正规方程
#include "complex.h"   // d_close / MATH_PI
#include <cmath>
#include <cstddef>

namespace nefu {
namespace mathext {

// ---------------- 描述统计 ----------------
double stat_mean(const double* x, int n) {
    if (n <= 0) return 0.0;
    double s = 0.0;
    for (int i = 0; i < n; i++) s += x[i];
    return s / (double)n;
}

double stat_variance(const double* x, int n) {
    if (n <= 0) return 0.0;
    double m = stat_mean(x, n);
    double s = 0.0;
    for (int i = 0; i < n; i++) { double d = x[i] - m; s += d * d; }
    return s / (double)n;
}

double stat_variance_sample(const double* x, int n) {
    if (n <= 1) return 0.0;
    double m = stat_mean(x, n);
    double s = 0.0;
    for (int i = 0; i < n; i++) { double d = x[i] - m; s += d * d; }
    return s / (double)(n - 1);
}

double stat_stddev(const double* x, int n) {
    return std::sqrt(stat_variance(x, n));
}

double stat_min(const double* x, int n) {
    if (n <= 0) return 0.0;
    double m = x[0];
    for (int i = 1; i < n; i++) if (x[i] < m) m = x[i];
    return m;
}

double stat_max(const double* x, int n) {
    if (n <= 0) return 0.0;
    double m = x[0];
    for (int i = 1; i < n; i++) if (x[i] > m) m = x[i];
    return m;
}

// 插入排序（拷贝上做，不污染原数组）
static void sort_copy(double* dst, const double* src, int n) {
    for (int i = 0; i < n; i++) dst[i] = src[i];
    for (int i = 1; i < n; i++) {
        double key = dst[i];
        int j = i - 1;
        while (j >= 0 && dst[j] > key) { dst[j + 1] = dst[j]; j--; }
        dst[j + 1] = key;
    }
}

double stat_median(const double* x, int n) {
    if (n <= 0) return 0.0;
    double* s = new double[(size_t)n];
    sort_copy(s, x, n);
    double m;
    if (n & 1) m = s[n / 2];
    else m = 0.5 * (s[n / 2 - 1] + s[n / 2]);
    delete[] s;
    return m;
}

double stat_percentile(const double* x, int n, double p) {
    if (n <= 0) return 0.0;
    if (p < 0) p = 0;
    if (p > 1) p = 1;
    double* s = new double[(size_t)n];
    sort_copy(s, x, n);
    double pos = p * (double)(n - 1);
    int lo = (int)std::floor(pos);
    int hi = lo + 1;
    if (hi >= n) { delete[] s; return s[n - 1]; }
    double frac = pos - (double)lo;
    double v = s[lo] + frac * (s[hi] - s[lo]);
    delete[] s;
    return v;
}

double stat_iqr(const double* x, int n) {
    return stat_percentile(x, n, 0.75) - stat_percentile(x, n, 0.25);
}

double stat_mode(const double* x, int n) {
    if (n <= 0) return 0.0;
    double* s = new double[(size_t)n];
    sort_copy(s, x, n);
    double best = s[0];
    int best_cnt = 1, cur_cnt = 1;
    for (int i = 1; i < n; i++) {
        if (d_close(s[i], s[i - 1], 1e-9)) cur_cnt++;
        else cur_cnt = 1;
        if (cur_cnt > best_cnt) { best_cnt = cur_cnt; best = s[i]; }
    }
    delete[] s;
    return best;
}

// ---------------- 相关 ----------------
double stat_covariance(const double* x, const double* y, int n) {
    if (n <= 0) return 0.0;
    double mx = stat_mean(x, n), my = stat_mean(y, n);
    double s = 0.0;
    for (int i = 0; i < n; i++) s += (x[i] - mx) * (y[i] - my);
    return s / (double)n;
}

double stat_correlation(const double* x, const double* y, int n) {
    double sxy = stat_covariance(x, y, n);
    double sx = std::sqrt(stat_variance(x, n));
    double sy = std::sqrt(stat_variance(y, n));
    if (sx < 1e-300 || sy < 1e-300) return 0.0;
    return sxy / (sx * sy);
}

// ---------------- 线性回归 ----------------
void linreg(const double* x, const double* y, int n,
            double& slope, double& intercept) {
    if (n <= 0) { slope = 0; intercept = 0; return; }
    double mx = stat_mean(x, n), my = stat_mean(y, n);
    double sxx = 0.0, sxy = 0.0;
    for (int i = 0; i < n; i++) {
        sxx += (x[i] - mx) * (x[i] - mx);
        sxy += (x[i] - mx) * (y[i] - my);
    }
    slope = (sxx > 1e-300) ? sxy / sxx : 0.0;
    intercept = my - slope * mx;
}

// ---------------- 多项式回归（正规方程 + 高斯消元） ----------------
double* polyreg(const double* x, const double* y, int n, int k) {
    int m = k + 1;                       // 系数个数
    // 构造正规方程 (X^T X) c = X^T y
    Matrix A(m, m);
    A.set_all(0.0);
    double* b = new double[(size_t)m];
    for (int i = 0; i < m; i++) b[i] = 0.0;
    for (int i = 0; i < n; i++) {
        double xp[16];                  // x 的各次幂，m<=15 足够
        xp[0] = 1.0;
        for (int p = 1; p < m; p++) xp[p] = xp[p - 1] * x[i];
        for (int r = 0; r < m; r++) {
            for (int c = 0; c < m; c++) A(r, c) += xp[r] * xp[c];
            b[r] += xp[r] * y[i];
        }
    }
    double* c = mat_solve(A, b);
    delete[] b;
    if (!c) { c = new double[(size_t)m]; for (int i = 0; i < m; i++) c[i] = 0.0; }
    return c;
}

// ---------------- 假设检验 ----------------
double z_test(const double* x, int n, double mu0, double sigma) {
    if (n <= 0 || sigma < 1e-300) return 0.0;
    double se = sigma / std::sqrt((double)n);
    return (stat_mean(x, n) - mu0) / se;
}

double t_test(const double* x, int n, double mu0) {
    if (n <= 1) return 0.0;
    double s = std::sqrt(stat_variance_sample(x, n));
    if (s < 1e-300) return 0.0;
    double se = s / std::sqrt((double)n);
    return (stat_mean(x, n) - mu0) / se;
}

// ---------------- 直方图 ----------------
void histogram(const double* x, int n, double lo, double hi,
               int nb, int* counts) {
    for (int i = 0; i < nb; i++) counts[i] = 0;
    double width = hi - lo;
    if (width < 1e-300 || nb <= 0) return;
    for (int i = 0; i < n; i++) {
        int idx = (int)((x[i] - lo) / width * (double)nb);
        if (idx < 0) idx = 0;
        if (idx >= nb) idx = nb - 1;
        counts[idx]++;
    }
}

// ---------------- 随机数 ----------------
// 线性同余发生器（LCG，Numerical Recipes 参数）：快速、可复现。
static unsigned int s_rng = 123456789u;

void rng_seed(unsigned int s) { s_rng = s; }

double rng_uniform() {
    s_rng = s_rng * 1664525u + 1013904223u;
    return (double)(s_rng >> 1) / (double)(0x80000000u);
}

double rng_normal(double mu, double sd) {
    // Box-Muller：两个独立均匀 -> 标准正态
    double u1 = rng_uniform();
    double u2 = rng_uniform();
    if (u1 < 1e-300) u1 = 1e-300;
    double mag = std::sqrt(-2.0 * std::log(u1));
    double z = mag * std::cos(2.0 * MATH_PI * u2);
    return mu + sd * z;
}

double rng_exponential(double lambda) {
    if (lambda < 1e-300) lambda = 1e-300;
    double u = rng_uniform();
    if (u < 1e-300) u = 1e-300;
    return -std::log(u) / lambda;
}

int rng_poisson(double lambda) {
    // Knuth：不断累乘均匀数直到超过 e^{-lambda}
    double L = std::exp(-lambda);
    int k = 0;
    double p = 1.0;
    do {
        k++;
        p *= rng_uniform();
    } while (p > L && k < 100000);
    return k - 1;
}

// ---------------- 更多统计量 ----------------
double stat_harmonic_mean(const double* x, int n) {
    if (n <= 0) return 0.0;
    double s = 0.0;
    for (int i = 0; i < n; i++) {
        if (std::fabs(x[i]) < 1e-300) return 0.0;
        s += 1.0 / x[i];
    }
    return (s > 1e-300) ? (double)n / s : 0.0;
}

double stat_geometric_mean(const double* x, int n) {
    if (n <= 0) return 0.0;
    double s = 0.0;
    for (int i = 0; i < n; i++) {
        if (x[i] <= 0) return 0.0;
        s += std::log(x[i]);
    }
    return std::exp(s / (double)n);
}

double stat_median_abs_dev(const double* x, int n) {
    if (n <= 0) return 0.0;
    double med = stat_median(x, n);
    double* d = new double[(size_t)n];
    for (int i = 0; i < n; i++) d[i] = std::fabs(x[i] - med);
    double m = stat_median(d, n);
    delete[] d;
    return m;
}

double stat_trimmed_mean(const double* x, int n, double alpha) {
    if (n <= 0) return 0.0;
    double* s = new double[(size_t)n];
    sort_copy(s, x, n);
    int k = (int)(alpha * (double)n);
    if (k > n / 2) k = n / 2;
    double sum = 0.0;
    int cnt = 0;
    for (int i = k; i < n - k; i++) { sum += s[i]; cnt++; }
    delete[] s;
    return cnt > 0 ? sum / (double)cnt : 0.0;
}

double stat_spearman(const double* x, const double* y, int n) {
    if (n <= 1) return 0.0;
    // 把 x,y 分别转成秩（平均秩处理并列），再算 Pearson
    double* rx = new double[(size_t)n];
    double* ry = new double[(size_t)n];
    // 简单做法：对索引按值排序后赋秩
    int* idx = new int[(size_t)n];
    for (int i = 0; i < n; i++) idx[i] = i;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (x[idx[j]] < x[idx[i]]) { int t = idx[i]; idx[i] = idx[j]; idx[j] = t; }
    for (int i = 0; i < n; i++) rx[idx[i]] = (double)(i + 1);
    for (int i = 0; i < n; i++) idx[i] = i;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (y[idx[j]] < y[idx[i]]) { int t = idx[i]; idx[i] = idx[j]; idx[j] = t; }
    for (int i = 0; i < n; i++) ry[idx[i]] = (double)(i + 1);
    double r = stat_correlation(rx, ry, n);
    delete[] rx; delete[] ry; delete[] idx;
    return r;
}
// ---------------- 自检 ----------------
int statistics_self_test() {
    int fails = 0;
    const double EPS = 1e-6;

    // 1. mean / variance
    double d1[5] = {1, 2, 3, 4, 5};
    if (!d_close(stat_mean(d1, 5), 3.0, EPS)) fails++;
    if (!d_close(stat_variance(d1, 5), 2.0, EPS)) fails++;
    if (!d_close(stat_median(d1, 5), 3.0, EPS)) fails++;

    // 2. 线性回归 y = x
    double x[4] = {1, 2, 3, 4};
    double y[4] = {1, 2, 3, 4};
    double slope, intercept;
    linreg(x, y, 4, slope, intercept);
    if (!d_close(slope, 1.0, EPS)) fails++;
    if (!d_close(intercept, 0.0, EPS)) fails++;

    // 3. 相关系数 x vs x = 1；x vs y = x 也是 1
    if (!d_close(stat_correlation(x, x, 4), 1.0, EPS)) fails++;

    // 4. 百分位：[1,2,3,4,5] 的 Q2 = 3
    if (!d_close(stat_percentile(d1, 5, 0.5), 3.0, EPS)) fails++;

    // 5. 直方图：1000 个 [0,10) 均匀数落入 10 桶，每桶 ~100
    rng_seed(42);
    double* hdata = new double[1000];
    for (int i = 0; i < 1000; i++) hdata[i] = rng_uniform() * 10.0;
    int counts[10];
    histogram(hdata, 1000, 0, 10, 10, counts);
    int total = 0;
    for (int i = 0; i < 10; i++) total += counts[i];
    if (total != 1000) fails++;
    delete[] hdata;

    // 6. 正态抽样均值接近 mu=0（3000 个样本，容差放宽）
    rng_seed(7);
    double* nd = new double[3000];
    for (int i = 0; i < 3000; i++) nd[i] = rng_normal(5.0, 1.0);
    if (!d_close(stat_mean(nd, 3000), 5.0, 0.1)) fails++;
    delete[] nd;

    return fails;
}

} // namespace mathext
} // namespace nefu
