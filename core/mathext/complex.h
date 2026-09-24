// nefuOS 数学扩展库 —— 复数模块
// 纯 double 实现，无 STL / 异常 / RTTI，可在宿主(Win32)与裸机环境分别编译。
// 设计目标：作为矩阵特征值、多项式求根、统计回归等模块的底层支撑。
#pragma once

namespace nefu {
namespace mathext {

// 圆周率（不依赖 <cmath> 的 M_PI，避免 glibc/minglibc 差异）
const double MATH_PI = 3.14159265358979323846;

// ---------------- 复数结构体 ----------------
// 采用最朴素的实部/虚部二元组表示。所有运算都是纯函数，不修改入参，
// 返回值通过值语义返回（double 只有 16 字节，拷贝代价可忽略）。
struct Complex {
    double re;   // 实部
    double im;   // 虚部

    // 构造：默认 0；单参数视为纯实数；两参数显式指定 re/im
    Complex() : re(0.0), im(0.0) {}
    Complex(double r) : re(r), im(0.0) {}
    Complex(double r, double i) : re(r), im(i) {}

    // 相等比较（self_test 用近似比较，这里只提供严格相等，供 0/1 判定）
    bool operator==(const Complex& o) const { return re == o.re && im == o.im; }
    bool operator!=(const Complex& o) const { return !(*this == o); }
};

// ---------------- 基本四则运算 ----------------
// (a+bi)+(c+di) = (a+c)+(b+d)i
Complex c_add(const Complex& a, const Complex& b);
// (a+bi)-(c+di) = (a-c)+(b-d)i
Complex c_sub(const Complex& a, const Complex& b);
// (a+bi)*(c+di) = (ac-bd)+(ad+bc)i
// 验证值：(1+2i)*(3+4i) = (3-8)+(4+6)i = -5+10i
Complex c_mul(const Complex& a, const Complex& b);
// (a+bi)/(c+di) = ((ac+bd)+(bc-ad)i)/(c^2+d^2)
Complex c_div(const Complex& a, const Complex& b);

// ---------------- 标量运算 ----------------
Complex c_scale(const Complex& a, double s);   // 乘实数
Complex c_neg(const Complex& a);               // 取负

// ---------------- 共轭 / 模 / 辐角 ----------------
Complex c_conj(const Complex& a);              // a-bi
double  c_abs(const Complex& a);                // sqrt(re^2+im^2)
double  c_norm2(const Complex& a);             // |z|^2，避免开方
double  c_arg(const Complex& a);               // 辐角，范围 (-pi, pi]，atan2(im, re)

// ---------------- 极坐标转换 ----------------
// 由模 r 与辐角 theta 构造复数：r*(cos(theta)+i sin(theta))
Complex c_from_polar(double r, double theta);
// 拆解为 (模, 辐角)，out_r / out_theta 为出参
void    c_to_polar(const Complex& a, double& out_r, double& out_theta);

// ---------------- 初等函数 ----------------
// e^(a+bi) = e^a * (cos b + i sin b)
Complex c_exp(const Complex& a);
// 主值对数 ln(z) = ln|z| + i*arg(z)
Complex c_log(const Complex& a);
// 复数幂 z^w = exp(w * log z)（主值）
Complex c_pow(const Complex& a, const Complex& w);
// 整数次幂 z^n（快速幂，比 exp/log 精确且快）
Complex c_pow_int(const Complex& a, int n);
// 平方根：exp(0.5 * log z)
Complex c_sqrt(const Complex& a);
// sin / cos：sin(a+bi) = sin a cosh b + i cos a sinh b
Complex c_sin(const Complex& a);
Complex c_cos(const Complex& a);

// ---------------- 单位根 ----------------
// n 次单位根 w = exp(2*pi*i/n)，第 k 个：w^k = exp(2*pi*i*k/n)
Complex unit_root(int n, int k);

// ---------------- 近似相等（self_test / 数值算法通用） ----------------
// 相对误差 + 绝对误差混合判据：|a-b| <= eps * max(1, |a|, |b|)
bool c_close(const Complex& a, const Complex& b, double eps);
bool d_close(double a, double b, double eps);

// ---------------- 双曲/反三角 ----------------
Complex c_tanh(const Complex& a);             // tanh
Complex c_asinh(const Complex& a);            // asinh
Complex c_acosh(const Complex& a);            // acosh
Complex c_atanh(const Complex& a);            // atanh
double  c_log10_real(double x);               // 常用对数（实数）
// ---------------- 自检 ----------------
// 用已知值验证全部运算：
//   (1+2i)*(3+4i) = -5+10i
//   (1+i)/(1-i) = i
//   exp(pi*i) + 1 = 0（欧拉恒等式）
// 返回失败条数，0 表示全部通过。
int complex_self_test();

} // namespace mathext
} // namespace nefu
