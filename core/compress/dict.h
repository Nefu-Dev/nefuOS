// nefuOS 压缩算法库 —— 现代快速压缩格式（LZ4 / Snappy 简化教学版）
//
// 实现：
//   1. lz4_encode / lz4_decode —— LZ4 帧格式简化版。
//      编码块由一系列 token 组成：字面量长度 + 字面量 + (offset, matchlen)。
//      追求极快解压，压缩率中等。
//   2. snappy_encode / snappy_decode —— Snappy 块格式简化版。
//      同样用 token 序列，但长度编码略有不同。
//   3. dict_match —— 通用滑动窗口 + 哈希链字典匹配器，供教学参考。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace compress {

int lz4_encode(const uint8_t* in, int n, uint8_t* out, int cap);
int lz4_decode(const uint8_t* in, int n, uint8_t* out, int cap);

int snappy_encode(const uint8_t* in, int n, uint8_t* out, int cap);
int snappy_decode(const uint8_t* in, int n, uint8_t* out, int cap);

int dict_self_test();

} // namespace compress
} // namespace nefu
