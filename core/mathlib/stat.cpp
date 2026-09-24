// nefuOS mathlib —— 统计实现 + 自测
#include "mathlib/stat.h"
#include <cstdio>

namespace nefu {
namespace mathx {

double Stat::mean(const std::vector<double>& x) {
    if (x.empty()) return 0;
    double s = 0;
    for (int i = 0; i < (int)x.size(); i++) s += x[i];
    return s / (double)x.size();
}

double Stat::variance(const std::vector<double>& x) {
    if (x.empty()) return 0;
    double m = mean(x), s = 0;
    for (int i = 0; i < (int)x.size(); i++) s += (x[i] - m) * (x[i] - m);
    return s / (double)x.size();
}

double Stat::stdev(const std::vector<double>& x) { return std::sqrt(variance(x)); }

double Stat::min(const std::vector<double>& x) {
    if (x.empty()) return 0;
    double r = x[0];
    for (int i = 1; i < (int)x.size(); i++) if (x[i] < r) r = x[i];
    return r;
}

double Stat::max(const std::vector<double>& x) {
    if (x.empty()) return 0;
    double r = x[0];
    for (int i = 1; i < (int)x.size(); i++) if (x[i] > r) r = x[i];
    return r;
}

double Stat::median(std::vector<double> x) {
    if (x.empty()) return 0;
    std::sort(x.begin(), x.end());
    int n = (int)x.size();
    if (n % 2 == 1) return x[n / 2];
    return (x[n / 2 - 1] + x[n / 2]) / 2.0;
}

double Stat::quantile(std::vector<double> x, double q) {
    if (x.empty()) return 0;
    std::sort(x.begin(), x.end());
    double pos = q * ((double)x.size() - 1.0);
    int lo = (int)pos;
    double frac = pos - lo;
    if (lo + 1 >= (int)x.size()) return x.back();
    return x[lo] * (1 - frac) + x[lo + 1] * frac;
}

double Stat::mode(const std::vector<double>& x) {
    if (x.empty()) return 0;
    // 简单计数：用平方探测近似（教学版对离散值）
    double best = x[0];
    int bestc = 0;
    for (int i = 0; i < (int)x.size(); i++) {
        int cnt = 0;
        for (int j = 0; j < (int)x.size(); j++) if (x[j] == x[i]) cnt++;
        if (cnt > bestc) { bestc = cnt; best = x[i]; }
    }
    return best;
}

void RunningStat::push(double x) {
    n++;
    double d = x - mean_;
    mean_ += d / (double)n;
    m2 += d * (x - mean_);
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
}

int stat_self_test() {
    g_fails = 0;
    {
        std::vector<double> x;
        x.push_back(1); x.push_back(2); x.push_back(3); x.push_back(4); x.push_back(5);
        expect("stat-mean", Stat::mean(x) == 3.0);
        expect("stat-minmax", Stat::min(x) == 1 && Stat::max(x) == 5);
        expect("stat-variance", std::abs(Stat::variance(x) - 2.0) < 1e-9);
        expect("stat-stdev", std::abs(Stat::stdev(x) - std::sqrt(2.0)) < 1e-9);
        expect("stat-median", Stat::median(x) == 3.0);
        expect("stat-range", Stat::range(x) == 4.0);
    }
    {
        std::vector<double> y;
        y.push_back(1); y.push_back(2); y.push_back(2); y.push_back(3);
        expect("stat-mode", Stat::mode(y) == 2);
        expect("stat-median-even", Stat::median(y) == 2.0);
        expect("stat-quantile", std::abs(Stat::quantile(y, 0.25) - 1.75) < 1e-9);
    }
    {
        RunningStat rs;
        rs.push(1); rs.push(2); rs.push(3); rs.push(4); rs.push(5);
        expect("running-mean", std::abs(rs.mean() - 3.0) < 1e-9);
        expect("running-count", rs.count() == 5);
        expect("running-stdev", std::abs(rs.stdev() - std::sqrt(2.0)) < 1e-9);
    }
    return g_fails;
}

} // namespace mathx
} // namespace nefu
