// nefuOS mathlib —— 回归分析 regression
// 教学版：一元线性回归 y = a + b*x（最小二乘）+ 相关系数 r。
#pragma once
#include <vector>

namespace nefu {
namespace mathx {

// 一元线性回归结果
struct LinearFit {
    double a;        // 截距
    double b;        // 斜率
    double r;        // 相关系数（-1..1）
    double r2;       // 决定系数 R^2

    // 由 (x,y) 样本点拟合
    static LinearFit fit(const std::vector<double>& x, const std::vector<double>& y);
    double predict(double x) const { return a + b * x; }
};

int regression_self_test();

} // namespace mathx
} // namespace nefu
