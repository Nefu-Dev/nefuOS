// nefuOS crypto library — AES-128 (aes)
// AES：高级加密标准（Rijndael），128 位分组、128/192/256 位密钥。
// 本实现为 AES-128 + ECB 模式（教学版；真实项目务必用 CBC/GCM +
// 随机 IV）。流程：10 轮（1 轮初始 AddRoundKey + 9 轮完整 + 1 轮收尾），
// 每轮含 SubBytes(S盒替换) / ShiftRows(行移位) / MixColumns(列混合，
// 最后一轮省略) / AddRoundKey(轮密钥异或)。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace crypt {

// AES-128 ECB 加密：16 字节块 in → 16 字节 out；密钥 16 字节
void aes128_encrypt_block(const unsigned char key[16],
                          const unsigned char in[16], unsigned char out[16]);
// AES-128 ECB 解密
void aes128_decrypt_block(const unsigned char key[16],
                          const unsigned char in[16], unsigned char out[16]);
// 数据加密（长度 16 的倍数）；原地安全
void aes128_encrypt(const unsigned char key[16],
                    const unsigned char* in, int n, unsigned char* out);
void aes128_decrypt(const unsigned char key[16],
                    const unsigned char* in, int n, unsigned char* out);

int aes_self_test();

} // namespace crypt
} // namespace nefu
