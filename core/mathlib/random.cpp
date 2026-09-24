// nefuOS mathlib —— 随机数实现 + 自测
#include "mathlib/random.h"
#include <cstdio>

namespace nefu {
namespace mathx {

LCG::LCG(uint64_t seed) : state(seed ? seed : 1) {}

uint32_t LCG::next() {
    // 经典 LCG：x_{n+1} = (a*x_n + c) mod 2^32
    state = state * 1664525ULL + 1013904223ULL;
    return (uint32_t)(state >> 32) ^ (uint32_t)state;
}

double LCG::unit() { return (double)(next() & 0xFFFFFF) / (double)0x1000000; }

int LCG::range(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (int)(next() % (uint32_t)(hi - lo + 1));
}

double LCG::normal(double mu, double sigma) {
    // Box-Muller：用两个均匀数生成正态
    double u1 = unit();
    if (u1 < 1e-12) u1 = 1e-12;
    double u2 = unit();
    double z = std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * 3.14159265358979 * u2);
    return mu + sigma * z;
}

double LCG::poisson(double lambda) {
    // 反变换法（lambda 较小时）
    double L = std::exp(-lambda);
    double p = 1.0;
    int k = 0;
    do {
        k++;
        p *= unit();
    } while (p > L && k < 100000);
    return k - 1;
}

void LCG::shuffle(std::vector<int>& v) {
    for (int i = (int)v.size() - 1; i > 0; i--) {
        int j = range(0, i);
        int t = v[i]; v[i] = v[j]; v[j] = t;
    }
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
}

int random_self_test() {
    g_fails = 0;
    {
        LCG r(42);
        expect("lcg-range", r.range(0, 0) == 0);
        bool in = true;
        for (int i = 0; i < 1000; i++) {
            int v = r.range(1, 6);
            if (v < 1 || v > 6) in = false;
        }
        expect("lcg-dice", in);
    }
    {
        LCG r(7);
        double mn = 0, mx = 0;
        for (int i = 0; i < 1000; i++) {
            double u = r.unit();
            if (u < mn) mn = u;
            if (u > mx) mx = u;
        }
        expect("lcg-unit-range", mn >= 0 && mx < 1.0);
    }
    {
        LCG r(99);
        // 正态：均值应接近 mu
        double sum = 0;
        for (int i = 0; i < 500; i++) sum += r.normal(5.0, 1.0);
        double avg = sum / 500.0;
        expect("lcg-normal", avg > 4.5 && avg < 5.5);
    }
    {
        LCG r(3);
        std::vector<int> v;
        for (int i = 0; i < 10; i++) v.push_back(i);
        r.shuffle(v);
        bool perm = true;
        for (int i = 0; i < 10; i++) perm = perm && (v[i] >= 0 && v[i] < 10);
        int sum = 0;
        for (int i = 0; i < 10; i++) sum += v[i];
        expect("lcg-shuffle", perm && sum == 45);
    }
    {
        LCG r(5);
        double s = 0;
        for (int i = 0; i < 100; i++) s += r.poisson(3.0);
        double avg = s / 100.0;
        expect("lcg-poisson", avg > 2.0 && avg < 4.2);
    }
    return g_fails;
}

} // namespace mathx
} // namespace nefu
