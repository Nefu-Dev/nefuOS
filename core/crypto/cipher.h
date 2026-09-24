// nefuOS 密码学库 —— 分组/流密码模块
// 本模块从零实现常用对称密码算法，教学友好，全部使用裸数组，无 STL、无异常。
// 覆盖：AES-128/192/256 (ECB/CBC/CTR)、DES、3DES、RC4、ChaCha20、Salsa20、
//       XOR 一次伪随机流、Blowfish、国密 SM4。
// 每个算法都给出标准参考实现；self_test() 用官方测试向量验证。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace crypto {

// =====================================================================
// AES —— 高级加密标准 (FIPS-197)
// 分组长度固定 16 字节；密钥长度 16/24/32 字节对应 10/12/14 轮。
// 这里给出轮密钥调度 + 单分组加解密原语，再在外层封装 ECB/CBC/CTR 模式。
// =====================================================================
struct AES {
    int      nk;        // 密钥字数（4/6/8）
    int      nr;        // 轮数（10/12/14）
    uint32_t rk[60];    // 扩展轮密钥（按 32 位字存储，大端语义）

    // 按密钥长度初始化：key_len 必须为 16/24/32
    void init(const uint8_t* key, int key_len);
    // 单分组加/解密 in[16] -> out[16]
    void encrypt_block(const uint8_t in[16], uint8_t out[16]) const;
    void decrypt_block(const uint8_t in[16], uint8_t out[16]) const;
};

// ---- 分组模式（要求输入长度是 16 的整数倍；PKCS#7 填充由调用方处理）----
// ECB：每个分组独立加解密
void aes_ecb_encrypt(const AES& a, const uint8_t* in, uint8_t* out, int len);
void aes_ecb_decrypt(const AES& a, const uint8_t* in, uint8_t* out, int len);
// CBC：需要 16 字节 IV；加密时 Ci = EK(Pi xor Ci-1)，解密相反
void aes_cbc_encrypt(const AES& a, const uint8_t iv[16],
                     const uint8_t* in, uint8_t* out, int len);
void aes_cbc_decrypt(const AES& a, const uint8_t iv[16],
                     const uint8_t* in, uint8_t* out, int len);
// CTR：计数器模式加解密同函数；counter 为 16 字节初始计数器块，逐块大端自增
void aes_ctr_crypt(const AES& a, uint8_t counter[16],
                   const uint8_t* in, uint8_t* out, int len);

// =====================================================================
// DES / 3DES —— 数据加密标准
// DES 分组 8 字节，64 位密钥（含 8 位奇偶校验，实际 56 位）。
// 3DES = DES-EDE：加密 = E(D(E))，两次或三次密钥。
// =====================================================================
// 由 8 字节密钥生成 16 轮 48 轮子密钥（解密时逆序使用）
// 子密钥共 48 位，存于 uint64_t 的低 48 位
void des_key_schedule(const uint8_t key[8], uint64_t sub[16]);
void des_encrypt_block(const uint64_t sub[16], const uint8_t in[8], uint8_t out[8]);
void des_decrypt_block(const uint64_t sub[16], const uint8_t in[8], uint8_t out[8]);

// 3DES：key 长度 16（双长 K1,K2）或 24（三长 K1,K2,K3），分组 8 字节
struct DES3 {
    uint64_t k1[16], k2[16], k3[16];
    bool     triple;          // true=三密钥，false=双密钥(K3=K1)
    void init(const uint8_t* key, int key_len);   // 16 或 24
    void encrypt_block(const uint8_t in[8], uint8_t out[8]);
    void decrypt_block(const uint8_t in[8], uint8_t out[8]);
};

// =====================================================================
// RC4 —— Rivest 流密码 (1987)
// 极其简单的 PRGA：S 盒 0..255 经 KSA 打乱，再用伪随机字节流异或明文。
// 注意：RC4 已被证明不安全（尤其 WEP），此处仅教学用途。
// =====================================================================
struct RC4 {
    uint8_t s[256];
    int     i, j;
    void init(const uint8_t* key, int key_len);
    // 加解密同函数：对 in 与密钥流异或写 out（可 in==out 原地）
    void crypt(const uint8_t* in, uint8_t* out, int len);
};

// =====================================================================
// ChaCha20 —— Daniel J. Bernstein (2008)，TLS 常用流密码
// 256 位密钥 + 32 位计数器 + 96 位 nonce，每 64 字节做 20 轮（double-round*10）。
// =====================================================================
// 生成 64 字节密钥流块（counter 为块计数器，nonce 12 字节）
void chacha20_block(const uint8_t key[32], uint32_t counter,
                    const uint8_t nonce[12], uint8_t out[64]);
// 加解密：in/out 长度任意
void chacha20_crypt(const uint8_t key[32], const uint8_t nonce[12],
                    uint32_t counter, const uint8_t* in, uint8_t* out, int len);

// =====================================================================
// Salsa20 —— Bernstein (2005)，eSTREAM 入选流密码
// 结构与 ChaCha 类似但用列/对角线旋转，64 字节密钥流块。
// =====================================================================
void salsa20_block(const uint8_t key[32], uint32_t counter,
                   const uint8_t nonce[8], uint8_t out[64]);
void salsa20_crypt(const uint8_t key[32], const uint8_t nonce[8],
                   uint32_t counter, const uint8_t* in, uint8_t* out, int len);

// =====================================================================
// XOR 流密码 —— 重复密钥异或（教学用，不安全）
// =====================================================================
void xor_crypt(const uint8_t* key, int keylen,
               const uint8_t* in, uint8_t* out, int len);

// =====================================================================
// Blowfish —— Bruce Schneier (1993)，分组 8 字节，可变密钥 4..56 字节
// 16 轮 Feistel，P 盒 18 个 32 位 + 4 个 S 盒(256 项)。
// =====================================================================
struct Blowfish {
    uint32_t P[18];
    uint32_t S[4][256];
    void init(const uint8_t* key, int key_len);
    void encrypt_block(const uint8_t in[8], uint8_t out[8]);
    void decrypt_block(const uint8_t in[8], uint8_t out[8]);
};

// =====================================================================
// SM4 —— 国密分组密码 (GM/T 0002-2012)
// 分组 16 字节，密钥 16 字节，32 轮非线性变换。
// =====================================================================
struct SM4 {
    uint32_t rk[32];
    void init(const uint8_t key[16]);
    // enc=true 加密，false 解密（轮密钥逆序）
    void crypt_block(const uint8_t in[16], uint8_t out[16], bool enc);
};

// =====================================================================
// 自测试：用官方测试向量验证所有算法，返回失败数（0 表示全部通过）
// =====================================================================
int cipher_self_test();

} // namespace crypto
} // namespace nefu
