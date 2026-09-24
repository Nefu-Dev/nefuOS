// nefuOS 密码学库 —— 消息认证码 (MAC) 模块
// 覆盖：HMAC(MD5/SHA1/SHA256/SHA512/SHA3-256)、CMAC-AES、Poly1305、CBC-MAC。
// HMAC 基于 lib/hash.h 的 MD5/SHA1 与本库的 SHA-256/SHA-512/SHA-3 实现。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace crypto {

// =====================================================================
// HMAC (RFC 2104)：H(K XOR opad, H(K XOR ipad, msg))
// =====================================================================
void hmac_md5(const uint8_t* key, int klen, const uint8_t* data, int dlen, uint8_t out[16]);
void hmac_sha1(const uint8_t* key, int klen, const uint8_t* data, int dlen, uint8_t out[20]);
void hmac_sha256(const uint8_t* key, int klen, const uint8_t* data, int dlen, uint8_t out[32]);
void hmac_sha512(const uint8_t* key, int klen, const uint8_t* data, int dlen, uint8_t out[64]);
void hmac_sha3_256(const uint8_t* key, int klen, const uint8_t* data, int dlen, uint8_t out[32]);

// =====================================================================
// CMAC-AES (NIST SP 800-38B)：基于 AES 的 MAC，输出 16 字节
// key_len 16/24/32
// =====================================================================
void cmac_aes(const uint8_t* key, int key_len,
              const uint8_t* data, int dlen, uint8_t out[16]);

// =====================================================================
// Poly1305 (RFC 8439)：一次一密钥的强 MAC，输出 16 字节
// =====================================================================
void poly1305_mac(const uint8_t key[32], const uint8_t* msg, int len, uint8_t out[16]);

// =====================================================================
// CBC-MAC（简化：AES-CBC 最后一个分组，无填充则视为已对齐）
// =====================================================================
void cbcmac_aes(const uint8_t* key, int key_len,
                const uint8_t* data, int dlen, uint8_t out[16]);

// 自测试：返回失败数（0 全部通过）
int mac_self_test();

} // namespace crypto
} // namespace nefu
