// nefuOS mathlib —— 扩展欧几里得 gcd
// 教学版：gcd/lcm/扩展欧几里得（求 ax+by=gcd）、同余方程、快速幂取模。
#pragma once
#include <cstdint>

namespace nefu {
namespace mathx {

struct Gcd {
    // 辗转相除
    static int64_t gcd(int64_t a, int64_t b);
    static int64_t lcm(int64_t a, int64_t b);

    // 扩展欧几里得：解 ax + by = gcd(a,b)，返回 gcd；x/y 为特解
    static int64_t exgcd(int64_t a, int64_t b, int64_t& x, int64_t& y);

    // 解线性同余方程 ax ≡ b (mod m)，返回最小非负解；无解返回 -1
    static int64_t solve_mod(int64_t a, int64_t b, int64_t m);

    // 快速幂取模 a^e mod m
    static int64_t powmod(int64_t a, int64_t e, int64_t m);
};

int gcd_self_test();

} // namespace mathx
} // namespace nefu
