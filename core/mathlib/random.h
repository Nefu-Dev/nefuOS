// nefuOS mathlib —— 随机数 random
// 教学版：LCG 伪随机 + 洗牌 + 常见分布（均匀/正态/泊松）。
#pragma once
#include <vector>
#include <cstdint>
#include <cmath>

namespace nefu {
namespace mathx {

// 线性同余生成器（可复现，教学演示）
struct LCG {
    uint64_t state;

    explicit LCG(uint64_t seed = 12345);
    uint32_t next();                        // 32 位随机
    double    unit();                       // [0,1) 均匀
    int       range(int lo, int hi);        // [lo, hi] 整数
    double    normal(double mu, double sigma);  // 正态（Box-Muller）
    double    poisson(double lambda);       // 泊松（反变换，lambda 较小）
    void      shuffle(std::vector<int>& v); // Fisher-Yates 洗牌
};

int random_self_test();

} // namespace mathx
} // namespace nefu
