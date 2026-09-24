// nefuOS mathlib —— 快速傅里叶变换 fft
// 教学版：迭代基-2 FFT（蝶形运算），复数数组，长度必须为 2 的幂。
#pragma once
#include <vector>
#include "mathlib/complex.h"

namespace nefu {
namespace mathx {

// 就地迭代 FFT：a 长度必须为 2 的幂
// inverse=false 正变换（时域->频域），inverse=true 逆变换（乘 1/n 后还原）
void fft(std::vector<Complex>& a, bool inverse);

// 方便封装：输入实数序列，返回频域复数序列
std::vector<Complex> fft_forward(const std::vector<double>& x);

// 用 FFT 做多项式乘法（卷积）：返回系数数组
std::vector<double> fft_convolve(const std::vector<double>& a, const std::vector<double>& b);

// 下一个不小于 n 的 2 的幂
int next_pow2(int n);

int fft_self_test();

} // namespace mathx
} // namespace nefu
