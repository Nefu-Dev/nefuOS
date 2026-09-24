// nefuOS compression library — Huffman 编码 (huffman)
// Huffman：基于频率的最优前缀编码（1952）。思想：出现越频繁的字符
// 用越短的码。构建方式：把每个符号当作叶子，反复合并权值最小的两棵树，
// 左 0 右 1 得到每个符号的码字。前缀性质保证解码无歧义。
// 本实现：静态 Huffman（先统计频率 → 建树 → 编码表 → 位流写入码字），
// 解码时重建同频率表。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace comp {

// 编码：in[0..n-1] → out；返回长度（-1 失败）
int huffman_encode(const unsigned char* in, int n, unsigned char* out, int outcap);
// 解码：in[0..n-1] → out；返回长度（-1 失败）
int huffman_decode(const unsigned char* in, int n, unsigned char* out, int outcap);

int huffman_self_test();

} // namespace comp
} // namespace nefu
