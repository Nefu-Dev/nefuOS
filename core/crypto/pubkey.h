// nefuOS 密码学库 —— 非对称密码模块
// 基于 core/lib/bigint.h 的 bignum::BigInt 实现：
//   RSA（密钥生成 / 加密 / 解密 / 签名 / 验证）
//   Diffie-Hellman 密钥交换
#pragma once
#include "lib/bigint.h"
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace crypto {

// ===================== RSA =====================
struct RSAKey {
    bignum::BigInt n, e, d;     // 公钥 (n,e)，私钥 d
};

// 生成密钥（bits 为 n 的位数；seed 为确定性种子，便于测试复现）
// bits 不宜过小（>=256），否则因子分解风险高。
void rsa_generate(RSAKey& k, int bits, uint64_t seed);

// 公钥加密：m（明文整数 < n）-> c
void rsa_encrypt_pub(const RSAKey& k, const bignum::BigInt& m, bignum::BigInt& c);
// 私钥解密：c -> m
void rsa_decrypt_priv(const RSAKey& k, const bignum::BigInt& c, bignum::BigInt& m);

// 签名：对消息哈希值 h 签名 s = h^d mod n
void rsa_sign(const RSAKey& k, const bignum::BigInt& h, bignum::BigInt& s);
// 验证：h' = s^e mod n，等于 h 返回 true
bool rsa_verify(const RSAKey& k, const bignum::BigInt& h, const bignum::BigInt& s);

// ===================== Diffie-Hellman =====================
struct DHParams {
    bignum::BigInt p;   // 大素数模数
    bignum::BigInt g;   // 生成元
};
// 生成 DH 参数（bits 位素数 p，g=2）
void dh_generate(DHParams& params, int bits, uint64_t seed);
// 由私钥 x（随机）计算公钥 g^x mod p
bignum::BigInt dh_public(const DHParams& params, const bignum::BigInt& x);
// 计算共享密钥：other_priv 与对方公钥
bignum::BigInt dh_shared(const DHParams& params, const bignum::BigInt& my_priv,
                         const bignum::BigInt& other_pub);

// 自测试：返回失败数（0 全部通过）
int pubkey_self_test();

} // namespace crypto
} // namespace nefu
