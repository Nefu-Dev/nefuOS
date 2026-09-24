// nefuOS mathlib —— 素数 prime
// 教学版：试除判断素数、埃拉托色尼筛、分解质因数、最大公约数辅助。
#pragma once
#include <vector>
#include <cstdint>

namespace nefu {
namespace mathx {

// 素数工具（全用 int64，教学版不用 Miller-Rabin）
struct Prime {
    // 朴素试除法判断素数
    static bool is_prime(int64_t n);
    // 埃氏筛：返回 [2, limit] 内所有素数
    static std::vector<int64_t> sieve(int limit);
    // 分解质因数：返回 (素数, 指数) 对
    static std::vector<std::pair<int64_t,int> > factors(int64_t n);
    // n 以内素数个数（π(n) 近似 + 精确小值）
    static int count_up_to(int n);
    // 第 k 个素数（k 从 1 起）
    static int64_t nth_prime(int k);
};

int prime_self_test();

} // namespace mathx
} // namespace nefu
