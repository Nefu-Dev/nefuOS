// nefuOS compression library — LZ77 滑动窗口 (lz77)
// LZ77（1977）：用"历史窗口"做字典压缩。编码时在窗口中查找最长
// 匹配串，输出三元组 (distance, length, next_char)。解码只需窗口
// 回看复制。gzip/zip 的 Deflate 即 LZ77 + Huffman 的组合。
// 教学版：窗口 4096、最大匹配 255、编码用三重循环朴素搜索。
#pragma once
#include <stddef.h>

namespace nefu {
namespace comp {

// 编码：in[0..n-1] → out；返回长度（-1 失败）
// 输出流：逐项三元组——[0x00 flag][dist_lo][dist_hi][len][char] 或 [0x01 flag][char]
int lz77_encode(const unsigned char* in, int n, unsigned char* out, int outcap);
// 解码
int lz77_decode(const unsigned char* in, int n, unsigned char* out, int outcap);

int lz77_self_test();

} // namespace comp
} // namespace nefu
