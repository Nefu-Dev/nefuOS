// nefuOS compression library — LZW 字典编码 (lzw)
// LZW（1984，Lempel–Ziv–Welch）：动态字典，边压缩边建表。
// 初始化 256 个单字符码，随后把"前缀+字符"逐步加入字典，
// 输出字典下标码字（GIF 图像即用 LZW）。编码表无需传递，
// 解码端同步重建，因此字典大小需要"重置点"控制。
// 教学版：字典 4096 项、12 位码宽、满后重置。
#pragma once
#include <stddef.h>

namespace nefu {
namespace comp {

// 编码：in[0..n-1] → out；返回长度（-1 失败）
int lzw_encode(const unsigned char* in, int n, unsigned char* out, int outcap);
// 解码
int lzw_decode(const unsigned char* in, int n, unsigned char* out, int outcap);

int lzw_self_test();

} // namespace comp
} // namespace nefu
