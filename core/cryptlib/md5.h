// nefuOS crypto library — MD5 (md5)
// MD5：经典 128 位散列。流程：填充（0x80 + 长度）→ 512 位分块 →
// 每块 64 轮（4 轮 × 16 步，含非线性函数 F/G/H/I 与常数表）。
// 注意：MD5 已不抗碰撞（2004 年王小云团队给出碰撞），教学中仅作
// 校验/历史学习用途；新项目推荐 SHA-256/SHA-3。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace crypt {

void md5(const unsigned char* data, int n, unsigned char out[16]);
void md5_str(const char* s, unsigned char out[16]);
void md5_hex(const unsigned char* out16, char* hexbuf);

int md5_self_test();

} // namespace crypt
} // namespace nefu
