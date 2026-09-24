// nefuOS mathlib —— 回归实现 + 自测
#include "mathlib/regression.h"
#include "mathlib/stat.h"
#include <cmath>
#include <cstdio>

namespace nefu {
namespace mathx {

LinearFit LinearFit::fit(const std::vector<double>& x, const std::vector<double>& y) {
    LinearFit f;
    f.a = f.b = f.r = f.r2 = 0;
    if (x.size() != y.size() || x.empty()) return f;
    int n = (int)x.size();
    double mx = Stat::mean(x);
    double my = Stat::mean(y);
    double sxx = 0, sxy = 0, syy = 0;
    for (int i = 0; i < n; i++) {
        double dx = x[i] - mx;
        double dy = y[i] - my;
        sxx += dx * dx;
        sxy += dx * dy;
        syy += dy * dy;
    }
    if (sxx == 0) return f;
    f.b = sxy / sxx;                 // 斜率
    f.a = my - f.b * mx;             // 截距
    if (syy > 0) {
        f.r = sxy / std::sqrt(sxx * syy);   // 相关系数
        f.r2 = f.r * f.r;
    }
    return f;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
}

int regression_self_test() {
    g_fails = 0;
    {
        // 完美直线 y = 2 + 3x
        std::vector<double> x, y;
        for (int i = 0; i < 10; i++) {
            x.push_back((double)i);
            y.push_back(2.0 + 3.0 * i);
        }
        LinearFit f = LinearFit::fit(x, y);
        expect("regress-slope", std::abs(f.b - 3.0) < 1e-9);
        expect("regress-intercept", std::abs(f.a - 2.0) < 1e-9);
        expect("regress-r", std::abs(f.r - 1.0) < 1e-9);
        expect("regress-predict", std::abs(f.predict(5) - 17.0) < 1e-9);
    }
    {
        // 有噪声的数据：斜率为 0.5 左右
        std::vector<double> x, y;
        for (int i = 0; i < 20; i++) {
            x.push_back((double)i);
            y.push_back(1.0 + 0.5 * i + (i % 3) * 0.01);
        }
        LinearFit f = LinearFit::fit(x, y);
        expect("regress-noise-slope", std::abs(f.b - 0.5) < 0.01);
        expect("regress-noise-a", std::abs(f.a - 1.0) < 0.05);
        expect("regress-noise-r2", f.r2 > 0.99);
    }
    return g_fails;
}

} // namespace mathx
} // namespace nefu
