// nefuOS 压缩算法库 —— 整数编码与差分编码
//
// 实现：
//   1. Delta 编码 / DPCM —— 相邻字节差分
//   2. Elias gamma / delta 编码
//   3. Golomb / Rice 编码（教学版，参数 k）
//   4. Fibonacci 编码
//   5. variable-byte / varint（LEB128 风格）
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace compress {

int delta_encode(const uint8_t* in, int n, uint8_t* out, int cap);
int delta_decode(const uint8_t* in, int n, uint8_t* out, int cap);

int dpcm_encode(const uint8_t* in, int n, uint8_t* out, int cap);
int dpcm_decode(const uint8_t* in, int n, uint8_t* out, int cap);

int varint_encode(const uint32_t* in, int n, uint8_t* out, int cap);
int varint_decode(const uint8_t* in, int n, uint32_t* out, int cap);

int elias_gamma_encode(const uint32_t* in, int n, uint8_t* out, int cap);
int elias_gamma_decode(const uint8_t* in, int n, uint32_t* out, int cap);

int elias_delta_encode(const uint32_t* in, int n, uint8_t* out, int cap);
int elias_delta_decode(const uint8_t* in, int n, uint32_t* out, int cap);

int other_self_test();

} // namespace compress
} // namespace nefu
