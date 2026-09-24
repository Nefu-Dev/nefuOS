// nefuOS compression library — 算术编码 (arithmetic)
// 算术编码：把整个消息编码为 [0,1) 区间上的一个实数。每个符号按
// 概率把当前区间细分，最终区间内任一点即可解码。相比 Huffman
// 可达到熵极限（尤其高频字符）。教学版用 32 位整数定点实现
// （避免浮点），按累计频率表缩放区间，最后输出区间低端点。
#pragma once
#include <stddef.h>

namespace nefu {
namespace comp {

// 编码：in[0..n-1] → out；返回长度（-1 失败）
// 表头：256 个符号频率（4 字节大端每个，实际教学限 16 位）+ 数据位流
int arithmetic_encode(const unsigned char* in, int n, unsigned char* out, int outcap);
// 解码
int arithmetic_decode(const unsigned char* in, int n, unsigned char* out, int outcap);

int arithmetic_self_test();

} // namespace comp
} // namespace nefu
