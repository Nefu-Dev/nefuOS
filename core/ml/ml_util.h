// nefuOS 机器学习库 —— 共享标量工具（全定点 Q16.16，无 FPU 依赖）
// 本头文件只放 inline 小工具，不对应独立 self_test；供 matrix/dataset/linear/
// knn/kmeans/pca/neuralnet/bayes 等模块复用。
//
// 设计要点：
//   * 所有浮点运算走 nefu::fx::fix（int32 Q16.16），禁止 double / float；
//   * 向量点积用 int64 累加原始乘积（Q32.32），避免大量 fx_mul 后 int32 溢出；
//   * 随机数用可种子 LCG，保证 self_test 可复现；
//   * -O2 下把清零/拷贝循环误判成 memset/memcpy 会栈崩溃，因此这里的循环
//     都写得朴素直接，调用方也不要对大数组用 memset。
#pragma once
#include <stdint.h>
#include "../lib/softmath.h"

namespace nefu {
namespace ml {

using fx::fix;
using fx::fix64;

// ---------------- 近似判据 ----------------
// 两个定点数是否在相对/绝对容差内相等。tol 为 Q16.16 绝对容差。
inline bool fx_close(fix a, fix b, fix tol) {
    fix d = a - b;
    if (d < 0) d = -d;
    return d <= tol;
}

// ---------------- 点积 / 范数（64 位累加防溢出） ----------------
// 原始点积：返回 Q32.32（未右移），= Σ a[i]*b[i]。
inline fix64 dot_raw(const fix* a, const fix* b, int n) {
    fix64 s = 0;
    for (int i = 0; i < n; i++) s += (fix64)a[i] * (fix64)b[i];
    return s;
}
// 标准点积：返回 Q16.16 = raw >> 16。
inline fix dot(const fix* a, const fix* b, int n) {
    return (fix)(dot_raw(a, b, n) >> 16);
}
// L2 平方范数：Σ x[i]^2（Q32.32，未开方）。
inline fix64 sqnorm_raw(const fix* x, int n) { return dot_raw(x, x, n); }
// L2 范数：sqrt(Σ x[i]^2)，Q16.16。
inline fix l2norm(const fix* x, int n) {
    fix64 s = sqnorm_raw(x, n);
    if (s <= 0) return 0;
    // s 是 Q32.32；fx_sqrt 期望 Q16.16，先右移 16 位转回 Q16.16 再开方，
    // 开方后结果量级不变（sqrt(Q16.16) 仍 Q16.16）。
    fix arg = (fix)(s >> 16);
    if (arg < 0) arg = 0;
    return fx::fx_sqrt(arg);
}
// 欧氏距离平方：Σ (a[i]-b[i])^2，Q16.16。
inline fix dist2_euclid(const fix* a, const fix* b, int n) {
    fix64 s = 0;
    for (int i = 0; i < n; i++) {
        fix d = a[i] - b[i];
        s += (fix64)d * d;
    }
    return (fix)(s >> 16);
}
// 欧氏距离。
inline fix dist_euclid(const fix* a, const fix* b, int n) {
    fix d2 = dist2_euclid(a, b, n);
    if (d2 <= 0) return 0;
    return fx::fx_sqrt(d2);
}
// 曼哈顿距离：Σ |a[i]-b[i]|。
inline fix dist_manhattan(const fix* a, const fix* b, int n) {
    fix s = 0;
    for (int i = 0; i < n; i++) {
        fix d = a[i] - b[i];
        if (d < 0) d = -d;
        s += d;
    }
    return s;
}

// ---------------- 激活 / 压缩函数 ----------------
// sigmoid(x) = 1 / (1 + e^{-x})。fx_exp 内部已截断指数，不会溢出崩溃。
inline fix sigmoid(fix x) {
    fix e = fx::fx_exp((fix)(-x));          // e^{-x}
    fix den = fx::FX_ONE + e;               // 1 + e^{-x}
    return fx::fx_div(fx::FX_ONE, den);     // 1/den
}
// ReLU(x) = max(0, x)。
inline fix relu(fix x) { return x > 0 ? x : 0; }
// ReLU 导数：x>0 ? 1 : 0。
inline fix relu_grad(fix x) { return x > 0 ? fx::FX_ONE : 0; }
// tanh(x) = (e^{2x}-1)/(e^{2x}+1)，用 fx_exp 实现。
inline fix tanh_fx(fix x) {
    fix twox = (fix64)x << 1;               // 2x（Q16.16 左移一位即乘 2）
    fix e = fx::fx_exp(twox);
    fix num = e - fx::FX_ONE;
    fix den = e + fx::FX_ONE;
    return fx::fx_div(num, den);
}
// 恒等激活（回归头）。
inline fix linear_act(fix x) { return x; }

// ---------------- 可种子 LCG 随机数 ----------------
// 确定性随机源：self_test 用固定种子保证可复现；mllab 演示用时间种子。
struct Rng {
    uint32_t s;
    Rng() : s(0x1234567u) {}
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    // 下一个 [0, 2^32)
    uint32_t next_u32() {
        s = s * 1664525u + 1013904223u;
        return s;
    }
    // 均匀 [0, n) 整数
    int next_int(int n) {
        if (n <= 1) return 0;
        return (int)(next_u32() % (uint32_t)n);
    }
    // 均匀 [0, 1) 的 Q16.16
    fix next_fx() {
        uint32_t v = next_u32();
        return (fix)(v >> 16);              // 高 16 位即 Q16.16 的 [0,1)
    }
    // 均匀 [lo, hi) 的 Q16.16
    fix range_fx(fix lo, fix hi) {
        fix t = next_fx();                  // [0,1)
        return lo + fx::fx_mul(t, hi - lo);
    }
    // 从 n 个下标中无放回抽取 k 个，写入 out（长度 k）。
    void sample_noreplace(int* out, int k, int n) {
        // Fisher-Yates 部分洗牌：只洗前 k 个位置
        int idx[256];
        for (int i = 0; i < n && i < 256; i++) idx[i] = i;
        int m = n < 256 ? n : 256;
        for (int i = 0; i < k && i < m; i++) {
            int j = i + next_int(m - i);
            int t = idx[i]; idx[i] = idx[j]; idx[j] = t;
            out[i] = idx[i];
        }
    }
};

} // namespace ml
} // namespace nefu
