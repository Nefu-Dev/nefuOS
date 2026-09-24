// nefuOS mathlib —— 描述统计 stat
// 教学版：均值/方差/标准差/极值/中位数/众数/分位数，支持在线更新。
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace nefu {
namespace mathx {

// 一次性统计工具
struct Stat {
    // 一组样本
    static double mean(const std::vector<double>& x);
    static double variance(const std::vector<double>& x);   // 总体方差
    static double stdev(const std::vector<double>& x);      // 总体标准差
    static double min(const std::vector<double>& x);
    static double max(const std::vector<double>& x);
    static double median(std::vector<double> x);            // 排序后取中位数
    static double quantile(std::vector<double> x, double q); // 0..1 分位
    static double mode(const std::vector<double>& x);        // 众数（取第一个）
    static double range(const std::vector<double>& x) { return max(x) - min(x); }
};

// 在线均值/方差（流式更新，无需保存全部样本）
struct RunningStat {
    int64_t n;        // 样本数
    double  mean_;    // 当前均值
    double  m2;       // 与均值的平方差累计（Welford 算法）

    RunningStat() : n(0), mean_(0), m2(0) {}
    void push(double x);                       // 加入一个样本
    double mean() const { return n ? mean_ : 0; }
    double variance() const { return n > 1 ? m2 / (double)n : 0; }
    double stdev() const { return std::sqrt(variance()); }
    int64_t count() const { return n; }
};

int stat_self_test();

} // namespace mathx
} // namespace nefu
