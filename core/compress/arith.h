// nefuOS 压缩算法库 —— 算术编码 / 区间编码
//
// 实现：
//   1. arith_encode / arith_decode —— 32 位区间编码（Range Coder），
//      使用静态频率模型（先统计字节直方图，写在头部）。
//   2. 附带 QM-coder 的简化教学说明（不完整实现，仅注释）。
//
// 算术编码的思想：把整个消息映射成 [0,1) 区间里的一个小数，
// 每次根据符号概率把区间进一步细分。比 Huffman 更接近熵极限。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace compress {

int arith_encode(const uint8_t* in, int n, uint8_t* out, int cap);
int arith_decode(const uint8_t* in, int n, uint8_t* out, int cap);

int arith_self_test();

} // namespace compress
} // namespace nefu
