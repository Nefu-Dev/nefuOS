// nefuOS crypto library — SHA-1 (sha1)
// SHA-1：160 位散列（2017 年 Google 给出首个公开碰撞，不再用于安全场景；
// 教学/校验场景仍常见）。80 轮压缩 + 5 个状态字。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace crypt {

void sha1(const unsigned char* data, int n, unsigned char out[20]);
void sha1_str(const char* s, unsigned char out[20]);
void sha1_hex(const unsigned char* out20, char* hexbuf);

int sha1_self_test();

} // namespace crypt
} // namespace nefu
