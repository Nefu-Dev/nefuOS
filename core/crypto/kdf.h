// nefuOS 密码学库 —— 密钥派生函数 (KDF) 模块
// 覆盖：PBKDF2-HMAC-SHA256、HKDF-SHA256、scrypt（简化但真实 ROMix）、bcrypt（简化版）。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace crypto {

// PBKDF2 (RFC 2898)，PRF 固定为 HMAC-SHA256
//   pw/pwlen 口令；salt/saltlen 盐；iter 迭代次数；dk/dklen 输出密钥
void pbkdf2_hmac_sha256(const uint8_t* pw, int pwlen,
                        const uint8_t* salt, int saltlen,
                        unsigned int iter, uint8_t* dk, int dklen);

// HKDF (RFC 5869)
void hkdf_sha256(const uint8_t* salt, int saltlen,
                 const uint8_t* ikm, int ikmlen,
                 const uint8_t* info, int infolen,
                 uint8_t* okm, int oklen);

// scrypt (RFC 7914)：N 为 CPU/内存代价，r/p 为宽度参数。
// 教学实现，N 不宜过大（自测试用 N=16）。
void scrypt_kdf(const uint8_t* pw, int pwlen,
                const uint8_t* salt, int saltlen,
                uint64_t N, uint32_t r, uint32_t p,
                uint8_t* dk, int dklen);

// bcrypt 简化版：生成 $2a$cost$... 风格的 60 字符哈希串到 out[61]
// （基于 Blowfish 密钥扩展循环，cost 为 4..10 的 2^cost 轮）
void bcrypt_hash(const char* pw, const char* salt_b64, int cost, char* out);
// 校验口令是否匹配 bcrypt 串
bool bcrypt_verify(const char* pw, const char* hash);

// 自测试：返回失败数（0 全部通过）
int kdf_self_test();

} // namespace crypto
} // namespace nefu
