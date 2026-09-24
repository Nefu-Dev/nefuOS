// nefuOS 密码学库 —— 随机数生成器 (RNG) 模块
// 覆盖常见伪随机算法：MT19937、xorshift128+、xoshiro256**、LCG、PCG32、
// SplitMix64、AES-CTR_DRBG（基于 AES-256）、Well512a。
// 注意：这些是 PRNG，除 AES-CTR_DRBG 外均不适合做密码用途。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace crypto {

// ===================== MT19937 (1998) =====================
struct MT19937 {
    uint32_t mt[624];
    int idx;
    void seed(uint32_t s);
    uint32_t next();
};

// ===================== xorshift128+ =====================
// 状态 s0/s1 由调用方维护，返回下一个 64 位
uint64_t xorshift128plus(uint64_t* s0, uint64_t* s1);

// ===================== xoshiro256** =====================
// 状态 s[4]，返回下一个 64 位
uint64_t xoshiro256starstar(uint64_t s[4]);

// ===================== LCG（Numerical Recipes 式）=====================
uint32_t lcg_next(uint32_t* state);

// ===================== PCG32 (O'Neill) =====================
uint32_t pcg32_next(uint64_t* state, uint64_t inc);

// ===================== SplitMix64 =====================
uint64_t splitmix64(uint64_t* state);

// ===================== Well512a =====================
struct Well512a {
    uint32_t state[16];
    int index;
    void seed(uint32_t s);
    uint32_t next();
};

// ===================== AES-CTR_DRBG (NIST SP 800-90A 简化) =====================
// 用 AES-256 计数器生成密钥流；seed 为 32 字节密钥 + 16 字节计数器初值
struct AESCTRDRBG {
    uint8_t key[32];
    uint8_t counter[16];
    void init(const uint8_t seed[48]);   // 32 密钥 + 16 计数器
    void next(uint8_t* out, int len);
};

// 自测试：返回失败数（0 全部通过）
int rng_self_test();

} // namespace crypto
} // namespace nefu
