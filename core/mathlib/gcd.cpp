// nefuOS mathlib —— 扩展欧几里得实现 + 自测
#include "mathlib/gcd.h"
#include <cstdio>

namespace nefu {
namespace mathx {

int64_t Gcd::gcd(int64_t a, int64_t b) {
    a = a < 0 ? -a : a;
    b = b < 0 ? -b : b;
    while (b) { int64_t t = a % b; a = b; b = t; }
    return a;
}

int64_t Gcd::lcm(int64_t a, int64_t b) {
    if (a == 0 || b == 0) return 0;
    return a / gcd(a, b) * b;
}

int64_t Gcd::exgcd(int64_t a, int64_t b, int64_t& x, int64_t& y) {
    if (b == 0) { x = 1; y = 0; return a < 0 ? -a : a; }
    int64_t x1, y1;
    int64_t g = exgcd(b, a % b, x1, y1);
    x = y1;
    y = x1 - (a / b) * y1;
    return g;
}

int64_t Gcd::solve_mod(int64_t a, int64_t b, int64_t m) {
    int64_t x, y;
    int64_t g = exgcd(a, m, x, y);
    if (b % g != 0) return -1;
    int64_t step = m / g;
    int64_t r = (b / g * x) % step;
    if (r < 0) r += step;
    return r;
}

int64_t Gcd::powmod(int64_t a, int64_t e, int64_t m) {
    if (m <= 0) return 0;
    int64_t r = 1 % m;
    a %= m;
    while (e > 0) {
        if (e & 1) r = (r * a) % m;
        a = (a * a) % m;
        e >>= 1;
    }
    return r;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
}

int gcd_self_test() {
    g_fails = 0;
    {
        expect("gcd-12-18", Gcd::gcd(12, 18) == 6);
        expect("gcd-prime", Gcd::gcd(17, 13) == 1);
        expect("gcd-zero", Gcd::gcd(0, 5) == 5);
        expect("lcm", Gcd::lcm(4, 6) == 12);
    }
    {
        int64_t x, y;
        int64_t g = Gcd::exgcd(30, 12, x, y);
        expect("exgcd-g", g == 6);
        expect("exgcd-eq", 30 * x + 12 * y == 6);
    }
    {
        // 3x ≡ 4 (mod 7) -> x = 6 (3*6=18 ≡ 4)
        expect("solve-mod", Gcd::solve_mod(3, 4, 7) == 6);
        // 2x ≡ 3 (mod 4) 无解
        expect("solve-mod-none", Gcd::solve_mod(2, 3, 4) == -1);
        // x ≡ 0 (mod 5)
        expect("solve-mod-zero", Gcd::solve_mod(1, 5, 5) == 0);
    }
    {
        expect("powmod", Gcd::powmod(2, 10, 1000) == 24);
        expect("powmod-0", Gcd::powmod(7, 0, 13) == 1);
        expect("powmod-big", Gcd::powmod(5, 100, 7) == 2);   // 5^100 mod 7
    }
    return g_fails;
}

} // namespace mathx
} // namespace nefu
