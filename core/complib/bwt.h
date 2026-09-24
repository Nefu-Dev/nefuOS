// nefuOS compression library — Burrows–Wheeler 变换 (bwt)
// BWT（1994）：可逆的块重排变换，把文本变成"同类字符聚集"的形态，
// 再配合 RLE/MTF 大幅提升压缩率（bzip2 的核心前置）。
// 核心观察：循环移位排序后，最后一列（L）比原文本更"可压缩"；
// 逆变换只需 L 列 + 原串位置 I，通过 LF-mapping 重建。
// 附 MTF（Move-to-Front）编码：把频繁字符移到表头，产生大量小值。
#pragma once
#include <stddef.h>

namespace nefu {
namespace comp {

// BWT 编码：in[0..n-1] → out[0..n-1]（L 列）+ *primary（原串在排序中的行号）
// 返回 0 成功，-1 失败（n<=0）
int bwt_encode(const unsigned char* in, int n, unsigned char* out, int* primary);
// BWT 解码：L 列 + primary → 原数据
int bwt_decode(const unsigned char* in, int n, int primary, unsigned char* out);

// MTF 编码：值 0..255 表位置（字符集 256）
int mtf_encode(const unsigned char* in, int n, unsigned char* out);
int mtf_decode(const unsigned char* in, int n, unsigned char* out);

int bwt_self_test();

} // namespace comp
} // namespace nefu
