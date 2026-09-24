// nefuOS 数学扩展库 —— 多项式模块
// 系数按低次到高次存储：p(x) = c[0] + c[1]*x + ... + c[n-1]*x^(n-1)。
// 长度 n 即 degree+1；零多项式用长度 0 表示。
// 覆盖：Horner 求值、加减乘除（带余除法）、复合、GCD、导数/积分、
//       实根求解（符号扫描 + 牛顿精化 + 综合除法降次）、Taylor 展开。
#pragma once

namespace nefu {
namespace mathext {

// 释放多项式数组（new[] 分配的）。
void poly_free(double* p);

// ---------------- 基本属性 ----------------
int    poly_degree(const double* p, int n);          // 次数（零多项式返回 -1）
double poly_eval(const double* p, int n, double x);   // Horner 法求值 O(n)

// ---------------- 算术 ----------------
double* poly_add(const double* a, int na, const double* b, int nb, int& out_n);
double* poly_sub(const double* a, int na, const double* b, int nb, int& out_n);
double* poly_mul(const double* a, int na, const double* b, int nb, int& out_n);
// 带余除法：返回商式，余数写入 out_rem / out_rem_n。
double* poly_divmod(const double* a, int na, const double* b, int nb,
                    double*& out_rem, int& out_rem_n);
// 复合 p(q(x))
double* poly_compose(const double* p, int np, const double* q, int nq, int& out_n);

// ---------------- 微积分 ----------------
double* poly_derivative(const double* p, int n, int& out_n);
double* poly_integral(const double* p, int n, int& out_n);   // 常数项取 0

// ---------------- GCD（多项式 Euclid 算法） ----------------
double* poly_gcd(const double* a, int na, const double* b, int nb, int& out_n);

// ---------------- 根求解 ----------------
// 在区间 [lo, hi] 内求实根（符号变化扫描 + 牛顿精化），结果写入 out_roots，
// 最多 max_out 个；返回实际找到的根个数。
int poly_roots(const double* p, int n, double lo, double hi,
               double* out_roots, int max_out, double eps = 1e-10);

// ---------------- Taylor 展开 ----------------
// 把 p(x) 在 x=a 处展开：返回 q(t) = p(a+t) 的系数（低次到高次）。
double* poly_taylor(const double* p, int n, double a, int& out_n);

// ---------------- 插值 ----------------
// Lagrange 插值：过 (xi,yi) 共 n 个点，返回插值多项式系数（低次到高次）。
double* poly_lagrange(const double* xi, const double* yi, int n, int& out_n);
// Newton 插值：差商表构造，返回 Newton 形式系数。
double* poly_newton(const double* xi, const double* yi, int n, int& out_n);
// 用牛顿多项式求值（xi 为节点，coef 为差商系数）。
double  poly_newton_eval(const double* xi, const double* coef, int n, double x);
// ---------------- 自检 ----------------
// 已知值：
//   (x^2-3x+2) 在 x=2 处为 0，根为 1 和 2
//   (x^2-3x+2) = (x-1)(x-2)
//   导数 x^2 -> 2x
// 返回失败条数。
int poly_self_test();

} // namespace mathext
} // namespace nefu
