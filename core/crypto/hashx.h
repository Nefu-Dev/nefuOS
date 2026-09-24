// nefuOS 密码学库 —— 哈希/摘要模块
// 从零实现现代与经典密码杂凑算法（不重复 lib/hash.h 已有的 MD5/SHA1/CRC32）：
//   SHA-3 (Keccak) 224/256/384/512
//   SHA-512 / SHA-384
//   BLAKE2s（可截断到 1..32 字节）
//   MD4（经典，已被破解，仅教学）
//   RIPEMD-160（Bitcoin/ETH 地址用过）
//   国密 SM3
// 全部流式 init/update/final 接口，与 lib/hash.h 风格一致。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace crypto {

// =====================================================================
// SHA-3 (Keccak-f[1600] + 海绵结构)
// 支持 224/256/384/512 位输出；速率 rate = 200 - 2*dlen。
// =====================================================================
struct SHA3 {
    uint64_t state[25];      // 5x5 状态，共 1600 位
    uint8_t  buf[144];       // 最多 rate 字节（SHA3-256 rate=136）
    int      buflen;         // 当前缓冲字节数
    uint64_t bytes;          // 已处理字节数
    int      rate;           // 每块字节数
    int      hashlen;        // 输出字节数

    void init(int bits);     // bits = 224/256/384/512
    void update(const void* data, size_t n);
    void final(uint8_t* out);          // out 至少 hashlen 字节
    void hex_final(char* out);         // hex 字符串
};
// 一次性便捷函数
void sha3_224(const void* data, size_t n, uint8_t out[28]);
void sha3_256(const void* data, size_t n, uint8_t out[32]);
void sha3_384(const void* data, size_t n, uint8_t out[48]);
void sha3_512(const void* data, size_t n, uint8_t out[64]);

// =====================================================================
// SHA-512 / SHA-384（FIPS-180-4，64 位字，128 字节分组）
// =====================================================================
struct SHA512 {
    uint64_t h[8];
    uint64_t len;            // 已处理字节数
    uint8_t  buf[128];
    int      buflen;
    void init();
    void update(const void* data, size_t n);
    void final(uint8_t out[64]);
    void hex_final(char out[129]);
};
struct SHA384 {
    uint64_t h[8];
    uint64_t len;
    uint8_t  buf[128];
    int      buflen;
    void init();
    void update(const void* data, size_t n);
    void final(uint8_t out[48]);
    void hex_final(char out[97]);
};
void sha512(const void* data, size_t n, uint8_t out[64]);
void sha384(const void* data, size_t n, uint8_t out[48]);

// =====================================================================
// BLAKE2s (RFC 7693) —— 256 位级，输出 1..32 字节，可选密钥
// =====================================================================
struct BLAKE2s {
    uint32_t h[8];
    uint32_t t[2];           // 字节计数器（低/高）
    uint32_t f[2];           // 终态标志
    uint8_t  buf[64];
    int      buflen;
    int      outlen;
    void init(int out_len);              // out_len: 1..32
    void init_keyed(int out_len, const uint8_t* key, int keylen);
    void update(const void* data, size_t n);
    void final(uint8_t* out);
};
void blake2s(const void* data, size_t n, uint8_t* out, int out_len);

// =====================================================================
// MD4 (RFC 1320) —— 128 位，已不安全，教学用
// =====================================================================
struct MD4 {
    uint32_t a, b, c, d;
    uint64_t len;
    uint8_t  buf[64];
    int      buflen;
    void init();
    void update(const void* data, size_t n);
    void final(uint8_t out[16]);
    void hex_final(char out[33]);
};

// =====================================================================
// RIPEMD-160 (ISO/IEC 10118-3) —— 160 位
// =====================================================================
struct RIPEMD160 {
    uint32_t h[5];
    uint64_t len;
    uint8_t  buf[64];
    int      buflen;
    void init();
    void update(const void* data, size_t n);
    void final(uint8_t out[20]);
    void hex_final(char out[41]);
};

// =====================================================================
// SM3 国密杂凑 (GM/T 0004-2012) —— 256 位
// =====================================================================
struct SM3 {
    uint32_t h[8];
    uint64_t len;
    uint8_t  buf[64];
    int      buflen;
    void init();
    void update(const void* data, size_t n);
    void final(uint8_t out[32]);
    void hex_final(char out[65]);
};

// 自测试：用已知向量验证，返回失败数（0 全部通过）
int hashx_self_test();

} // namespace crypto
} // namespace nefu
