// nefuOS 数学扩展库 —— 统计与随机数模块
// 描述统计、相关回归、简化假设检验、直方图与常用分布随机数。
// 无 STL：数组由调用方提供，函数只做纯计算。
#pragma once

namespace nefu {
namespace mathext {

// ---------------- 描述统计 ----------------
double stat_mean(const double* x, int n);             // 算术平均
double stat_variance(const double* x, int n);         // 总体方差（除以 n）
double stat_variance_sample(const double* x, int n);  // 样本方差（除以 n-1）
double stat_stddev(const double* x, int n);           // 总体标准差
double stat_median(const double* x, int n);           // 中位数（拷贝后排序）
double stat_mode(const double* x, int n);             // 众数（最频繁值）
double stat_min(const double* x, int n);
double stat_max(const double* x, int n);
double stat_percentile(const double* x, int n, double p);  // p∈[0,1]，线性插值
double stat_iqr(const double* x, int n);              // 四分位距 Q3-Q1

// ---------------- 相关 ----------------
double stat_covariance(const double* x, const double* y, int n);  // 协方差
double stat_correlation(const double* x, const double* y, int n);  // 皮尔逊相关

// ---------------- 回归 ----------------
// 最小二乘线性拟合 y = a*x + b；输出 slope=a, intercept=b。
void linreg(const double* x, const double* y, int n,
            double& slope, double& intercept);
// 多项式回归：拟合 y = c0 + c1 x + ... + ck x^k（k 次）。
// 返回长度 k+1 的系数数组（低次到高次），调用方 delete[]。
double* polyreg(const double* x, const double* y, int n, int k);

// ---------------- 假设检验（简化） ----------------
// 单样本 z 检验：H0: mean = mu0，总体标准差 sigma 已知。返回 z 统计量。
double z_test(const double* x, int n, double mu0, double sigma);
// 单样本 t 检验：sigma 未知，用样本标准差。返回 t 统计量。
double t_test(const double* x, int n, double mu0);

// ---------------- 直方图 ----------------
// 把数据分到 nb 个桶（区间 [lo,hi]），counts 出参长度 nb。
void histogram(const double* x, int n, double lo, double hi,
               int nb, int* counts);

// ---------------- 随机数生成 ----------------
void   rng_seed(unsigned int s);
double rng_uniform();                    // [0,1) 均匀
double rng_normal(double mu, double sd); // 标准正态经 Box-Muller 变换
double rng_exponential(double lambda);   // 指数分布
int    rng_poisson(double lambda);       // 泊松分布（Knuth 算法）

// ---------------- 更多统计量 ----------------
double stat_harmonic_mean(const double* x, int n);    // 调和平均 n/sum(1/xi)
double stat_geometric_mean(const double* x, int n);    // 几何平均 (prod xi)^(1/n)
double stat_median_abs_dev(const double* x, int n);    // MAD = median(|xi-median|)
double stat_trimmed_mean(const double* x, int n, double alpha); // 去掉 alpha 两端后的均值
double stat_spearman(const double* x, const double* y, int n);  // Spearman 秩相关
// ---------------- 自检 ----------------
// 已知值：
//   mean([1,2,3,4,5]) = 3
//   linreg y=[1,2,3,4], x=[1,2,3,4] -> slope=1, intercept=0
//   correlation(x,x) = 1
// 返回失败条数。
int statistics_self_test();

} // namespace mathext
} // namespace nefu
