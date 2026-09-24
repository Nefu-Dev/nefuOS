// nefuOS crypto library — HMAC-SHA256 (hmac)
// HMAC：密钥哈希消息认证码。结构：
//   HMAC(K, m) = H((K' ^ opad) || H((K' ^ ipad) || m))
// 其中 K' = 密钥不足 64 字节补零、超长则先哈希。用途：API 签名、
// 消息完整性 + 来源认证、TOTP 动态口令。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace crypt {

// 计算 HMAC-SHA256：密钥 key[0..klen-1]，消息 msg[0..mlen-1]，
// 输出 32 字节到 out
void hmac_sha256(const unsigned char* key, int klen,
                 const unsigned char* msg, int mlen, unsigned char out[32]);
void hmac_sha256_hex(const unsigned char* out32, char* hexbuf);

int hmac_self_test();

} // namespace crypt
} // namespace nefu
