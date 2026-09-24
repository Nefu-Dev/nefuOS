// nefuOS crypto library — PBKDF2 (pbkdf2)
// PBKDF2：基于密码的密钥派生函数（RFC 2898）。对密码 + 盐做
// HMAC 迭代（iterations 次），输出任意长度派生密钥。暴力破解成本
// 由迭代次数控制。用途：密码存储（配合随机盐）、磁盘加密密钥。
// 结构：F(P, S, c, i) = U1 ^ U2 ^ ... ^ Uc，U1 = HMAC(P, S || i)
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace crypt {

// 派生密钥：密码 pwd[0..plen-1]，盐 salt[0..slen-1]，迭代次数 iter，
// 输出 dklen 字节到 out（dklen <= 64，教学上限）
void pbkdf2_sha256(const unsigned char* pwd, int plen,
                   const unsigned char* salt, int slen,
                   int iter, int dklen, unsigned char* out);

int pbkdf2_self_test();

} // namespace crypt
} // namespace nefu
