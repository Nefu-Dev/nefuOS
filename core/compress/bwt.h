// nefuOS 压缩算法库 —— Burrows-Wheeler 变换
//
// BWT 是 bzip2 的核心：把数据重排，使相同字符聚集到一起，
// 后面再接 MTF + 游程 + 熵编码就能得到高压缩率。
//
// 本模块实现：
//   1. bwt_transform / bwt_inverse  —— 正向 BWT 与逆 BWT（LF-mapping）
//   2. mtf_encode / mtf_decode     —— Move-To-Front 变换
//   3. bwt_encode / bwt_decode     —— BWT+MTF 组合（可存盘的 round-trip）
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace compress {

// 正向 BWT：in[0..n) -> out[0..n)，返回主索引（primary index）
// n 必须 > 0。out 容量 >= n。
int bwt_transform(const uint8_t* in, int n, uint8_t* out, int* primary);
// 逆 BWT：in[0..n) 是最后一列，p 是主索引，还原到 out。
int bwt_inverse(const uint8_t* in, int n, int primary, uint8_t* out);

// Move-To-Front：把出现过的字符移到字典表最前，编码近期常用的小值
void mtf_encode(const uint8_t* in, int n, uint8_t* out);
void mtf_decode(const uint8_t* in, int n, uint8_t* out);

// BWT+MTF 组合压缩（教学版，不含后续熵编码），可存盘
int bwt_encode(const uint8_t* in, int n, uint8_t* out, int cap);
int bwt_decode(const uint8_t* in, int n, uint8_t* out, int cap);

int bwt_self_test();

} // namespace compress
} // namespace nefu
