// nefuOS compression library — 游程编码 (rle)
// RLE：把连续重复字节压缩为"计数 + 值"。适合黑白位图、传真、
// 简单图形数据。本实现为字节级变体：
//   重复段（>=3 次）编码为 0x00 count value；
//   普通段逐字节原样输出，连续非重复用 0xFF count 前缀防歧义。
// 教学注释含"最长游程"概念。
#pragma once
#include <stddef.h>

namespace nefu {
namespace comp {

// 编码：in[0..n-1] → out；返回输出长度（-1 = 缓冲区不足）
int rle_encode(const unsigned char* in, int n, unsigned char* out, int outcap);
// 解码：in[0..n-1] → out；返回输出长度（-1 = 数据错误/不足）
int rle_decode(const unsigned char* in, int n, unsigned char* out, int outcap);

int rle_self_test();

} // namespace comp
} // namespace nefu
