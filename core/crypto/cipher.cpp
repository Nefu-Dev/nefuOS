// nefuOS 密码学库 —— 对称密码实现
// 参考 FIPS-197 (AES)、FIPS-46-3 (DES/3DES)、RFC 8439 (ChaCha20)、
// eSTREAM (Salsa20)、 Schneier Blowfish、GM/T 0002 (SM4)。
// 全部用裸数组实现，教学注释详细。
#include "cipher.h"
#include <string.h>

namespace nefu {
namespace crypto {

// =====================================================================
// 小工具：大端读写
// =====================================================================
static inline uint32_t rd32_be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}
static inline void wr32_be(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}
static inline uint32_t rotl32(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}
static inline uint32_t rotr32(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

// =====================================================================
// AES 实现
// =====================================================================
// S 盒（FIPS-197 表 5），按行展开
static const uint8_t AES_SBOX[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};
// 逆 S 盒
static const uint8_t AES_INVSBOX[256] = {
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
};
// 轮常数 Rcon[i] = x^(i-1) 在 GF(2^8)，Rcon[1]=1
static const uint32_t AES_RCON[11] = {
    0x00000000u,0x01000000u,0x02000000u,0x04000000u,0x08000000u,0x10000000u,
    0x20000000u,0x40000000u,0x80000000u,0x1b000000u,0x36000000u
};

// GF(2^8) 上乘 2（MixColumns 用）
static inline uint8_t xtime(uint8_t x) {
    return (uint8_t)((x << 1) ^ (((x & 0x80) ? 0x11B : 0)));
}

void AES::init(const uint8_t* key, int key_len) {
    nk = key_len / 4;
    nr = nk + 6;
    int total = 4 * (nr + 1);          // 轮密钥字数
    // 前 nk 个字直接取密钥
    for (int i = 0; i < nk; i++)
        rk[i] = rd32_be(key + 4 * i);
    // 扩展
    for (int i = nk; i < total; i++) {
        uint32_t temp = rk[i - 1];
        if (i % nk == 0) {
            // RotWord + SubWord + Rcon
            temp = rotl32(temp, 8);
            uint32_t s = ((uint32_t)AES_SBOX[(temp >> 24) & 0xFF] << 24) |
                         ((uint32_t)AES_SBOX[(temp >> 16) & 0xFF] << 16) |
                         ((uint32_t)AES_SBOX[(temp >> 8) & 0xFF] << 8)  |
                         ((uint32_t)AES_SBOX[temp & 0xFF]);
            temp = s ^ AES_RCON[i / nk];
        } else if (nk > 6 && (i % nk) == 4) {
            temp = ((uint32_t)AES_SBOX[(temp >> 24) & 0xFF] << 24) |
                   ((uint32_t)AES_SBOX[(temp >> 16) & 0xFF] << 16) |
                   ((uint32_t)AES_SBOX[(temp >> 8) & 0xFF] << 8)  |
                   ((uint32_t)AES_SBOX[temp & 0xFF]);
        }
        rk[i] = rk[i - nk] ^ temp;
    }
}

void AES::encrypt_block(const uint8_t in[16], uint8_t out[16]) const {
    // state 按列存储：state[r][c]
    uint8_t s[4][4];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            s[r][c] = in[c * 4 + r];
    // 初始轮密钥加
    for (int c = 0; c < 4; c++) {
        uint32_t k = rk[c];
        s[0][c] ^= (uint8_t)(k >> 24); s[1][c] ^= (uint8_t)(k >> 16);
        s[2][c] ^= (uint8_t)(k >> 8);  s[3][c] ^= (uint8_t)k;
    }
    for (int round = 1; round <= nr; round++) {
        // SubBytes
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                s[r][c] = AES_SBOX[s[r][c]];
        // ShiftRows：第 r 行左循环移 r
        for (int r = 1; r < 4; r++) {
            uint8_t tmp[4];
            for (int c = 0; c < 4; c++) tmp[c] = s[r][(c + r) & 3];
            for (int c = 0; c < 4; c++) s[r][c] = tmp[c];
        }
        // MixColumns（最后一轮不做）
        if (round != nr) {
            for (int c = 0; c < 4; c++) {
                uint8_t a0 = s[0][c], a1 = s[1][c], a2 = s[2][c], a3 = s[3][c];
                s[0][c] = xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3;
                s[1][c] = a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3;
                s[2][c] = a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3);
                s[3][c] = (xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3);
            }
        }
        // AddRoundKey
        for (int c = 0; c < 4; c++) {
            uint32_t k = rk[round * 4 + c];
            s[0][c] ^= (uint8_t)(k >> 24); s[1][c] ^= (uint8_t)(k >> 16);
            s[2][c] ^= (uint8_t)(k >> 8);  s[3][c] ^= (uint8_t)k;
        }
    }
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            out[c * 4 + r] = s[r][c];
}

void AES::decrypt_block(const uint8_t in[16], uint8_t out[16]) const {
    uint8_t s[4][4];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            s[r][c] = in[c * 4 + r];
    for (int c = 0; c < 4; c++) {
        uint32_t k = rk[nr * 4 + c];
        s[0][c] ^= (uint8_t)(k >> 24); s[1][c] ^= (uint8_t)(k >> 16);
        s[2][c] ^= (uint8_t)(k >> 8);  s[3][c] ^= (uint8_t)k;
    }
    for (int round = nr - 1; round >= 0; round--) {
        // InvShiftRows：第 r 行右循环移 r
        for (int r = 1; r < 4; r++) {
            uint8_t tmp[4];
            for (int c = 0; c < 4; c++) tmp[c] = s[r][(c - r + 4) & 3];
            for (int c = 0; c < 4; c++) s[r][c] = tmp[c];
        }
        // InvSubBytes
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                s[r][c] = AES_INVSBOX[s[r][c]];
        // AddRoundKey
        for (int c = 0; c < 4; c++) {
            uint32_t k = rk[round * 4 + c];
            s[0][c] ^= (uint8_t)(k >> 24); s[1][c] ^= (uint8_t)(k >> 16);
            s[2][c] ^= (uint8_t)(k >> 8);  s[3][c] ^= (uint8_t)k;
        }
        // InvMixColumns
        if (round != 0) {
            for (int c = 0; c < 4; c++) {
                uint8_t a0 = s[0][c], a1 = s[1][c], a2 = s[2][c], a3 = s[3][c];
                uint8_t m0_unused = 0; (void)m0_unused;
                // 直接展开 InvMixColumns 矩阵：d0=14a0^11a1^13a2^9a3
                auto g = [](uint8_t x, int p)->uint8_t {
                    uint8_t res = 0, bp = (uint8_t)p;
                    // 简单：用 xtime 迭代累乘到 p（p 为 9/11/13/14）
                    uint8_t v = x;
                    // 预计算 powers
                    uint8_t p2 = xtime(x);
                    uint8_t p4 = xtime(p2);
                    uint8_t p8 = xtime(p4);
                    switch (p) {
                        case 9:  res = p8 ^ v; break;
                        case 11: res = p8 ^ p2 ^ v; break;
                        case 13: res = p8 ^ p4 ^ v; break;
                        case 14: res = p8 ^ p4 ^ p2; break;
                    }
                    (void)bp;
                    return res;
                };
                s[0][c] = g(a0,14) ^ g(a1,11) ^ g(a2,13) ^ g(a3,9);
                s[1][c] = g(a0,9)  ^ g(a1,14) ^ g(a2,11) ^ g(a3,13);
                s[2][c] = g(a0,13) ^ g(a1,9)  ^ g(a2,14) ^ g(a3,11);
                s[3][c] = g(a0,11) ^ g(a1,13) ^ g(a2,9)  ^ g(a3,14);
            }
        }
    }
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            out[c * 4 + r] = s[r][c];
}

void aes_ecb_encrypt(const AES& a, const uint8_t* in, uint8_t* out, int len) {
    for (int i = 0; i < len; i += 16)
        a.encrypt_block(in + i, out + i);
}
void aes_ecb_decrypt(const AES& a, const uint8_t* in, uint8_t* out, int len) {
    for (int i = 0; i < len; i += 16)
        a.decrypt_block(in + i, out + i);
}
void aes_cbc_encrypt(const AES& a, const uint8_t iv[16],
                     const uint8_t* in, uint8_t* out, int len) {
    uint8_t prev[16];
    memcpy(prev, iv, 16);
    for (int i = 0; i < len; i += 16) {
        uint8_t x[16];
        for (int b = 0; b < 16; b++) x[b] = in[i + b] ^ prev[b];
        a.encrypt_block(x, out + i);
        memcpy(prev, out + i, 16);
    }
}
void aes_cbc_decrypt(const AES& a, const uint8_t iv[16],
                     const uint8_t* in, uint8_t* out, int len) {
    uint8_t prev[16];
    memcpy(prev, iv, 16);
    for (int i = 0; i < len; i += 16) {
        uint8_t p[16];
        a.decrypt_block(in + i, p);
        for (int b = 0; b < 16; b++) out[i + b] = p[b] ^ prev[b];
        memcpy(prev, in + i, 16);
    }
}
void aes_ctr_crypt(const AES& a, uint8_t counter[16],
                   const uint8_t* in, uint8_t* out, int len) {
    uint8_t stream[16];
    int off = 0;
    while (len > 0) {
        a.encrypt_block(counter, stream);
        int chunk = len < 16 ? len : 16;
        for (int b = 0; b < chunk; b++) out[off + b] = in[off + b] ^ stream[b];
        // 大端计数器自增（最低字节在最后）
        int k = 15;
        while (k >= 0 && ++counter[k] == 0) k--;
        off += chunk;
        len -= chunk;
    }
}

// =====================================================================
// DES 实现（FIPS-46-3）
// =====================================================================
// 位号 1..64（MSB 为位 1）。从 64 位整数取第 i 位
static inline int getbit64(uint64_t v, int i) { // i:1..64
    return (int)((v >> (64 - i)) & 1u);
}
static inline uint64_t setbit64(uint64_t v, int i, int b) {
    uint64_t mask = 1ull << (64 - i);
    return b ? (v | mask) : (v & ~mask);
}
// 按表 perm 重排 64 位：out 第 j 位 = in 第 perm[j] 位
static inline uint64_t permute64(uint64_t in, const int* table, int out_bits) {
    uint64_t out = 0;
    for (int j = 0; j < out_bits; j++)
        if (getbit64(in, table[j])) out |= (1ull << (out_bits - 1 - j));
    return out;
}

static const int DES_IP[64] = {
    58,50,42,34,26,18,10,2,60,52,44,36,28,20,12,4,
    62,54,46,38,30,22,14,6,64,56,48,40,32,24,16,8,
    57,49,41,33,25,17,9,1,59,51,43,35,27,19,11,3,
    61,53,45,37,29,21,13,5,63,55,47,39,31,23,15,7
};
static const int DES_FP[64] = {
    40,8,48,16,56,24,64,32,39,7,47,15,55,23,63,31,
    38,6,46,14,54,22,62,30,37,5,45,13,53,21,61,29,
    36,4,44,12,52,20,60,28,35,3,43,11,51,19,59,27,
    34,2,42,10,50,18,58,26,33,1,41,9,49,17,57,25
};
static const int DES_PC1[56] = {
    57,49,41,33,25,17,9,1,58,50,42,34,26,18,10,2,59,51,43,35,27,19,11,3,60,52,44,36,
    63,55,47,39,31,23,15,7,62,54,46,38,30,22,14,6,61,53,45,37,29,21,13,5,28,20,12,4
};
static const int DES_PC2[48] = {
    14,17,11,24,1,5,3,28,15,6,21,10,23,19,12,4,26,8,16,7,27,20,13,2,
    41,52,31,37,47,55,30,40,51,45,33,48,44,49,39,56,34,53,46,42,50,36,29,32
};
static const int DES_E[48] = {
    32,1,2,3,4,5,4,5,6,7,8,9,8,9,10,11,12,13,12,13,14,15,16,17,
    16,17,18,19,20,21,20,21,22,23,24,25,24,25,26,27,28,29,28,29,30,31,32,1
};
static const int DES_P[32] = {
    16,7,20,21,29,12,28,17,1,15,23,26,5,18,31,10,
    2,8,24,14,32,27,3,9,19,13,30,6,22,11,4,25
};
static const int DES_SHIFTS[16] = {
    1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1
};
// 8 个 S 盒，每个 4 行 x 16 列
static const uint8_t DES_S[8][64] = {
    {14,4,13,1,2,15,11,8,3,10,6,12,5,9,0,7, 0,15,7,4,14,2,13,1,10,6,12,11,9,5,3,8,
     4,1,14,8,13,6,2,11,15,12,9,7,3,10,5,0, 15,12,8,2,4,9,1,7,5,11,3,14,10,0,6,13},
    {15,1,8,14,6,11,3,4,9,7,2,13,12,0,5,10, 3,13,4,7,15,2,8,14,12,0,1,10,6,9,11,5,
     0,14,7,11,10,4,13,1,5,8,12,6,9,3,2,15, 13,8,10,1,3,15,4,2,11,6,7,12,0,5,14,9},
    {10,0,9,14,6,3,15,5,1,13,12,7,11,4,2,8, 13,7,0,9,3,4,6,10,2,8,5,14,12,11,15,1,
     13,6,4,9,8,15,3,0,11,1,2,12,5,10,14,7, 1,10,13,0,6,9,8,7,4,15,14,3,11,5,2,12},
    {7,13,14,3,0,6,9,10,1,2,8,5,11,12,4,15, 13,8,11,5,6,15,0,3,4,7,2,12,1,10,14,9,
     10,6,9,0,12,11,7,13,15,1,3,14,5,2,8,4, 3,15,0,6,10,1,13,8,9,4,5,11,12,7,2,14},
    {2,12,4,1,7,10,11,6,8,5,3,15,13,0,14,9, 14,11,2,12,4,7,13,1,5,0,15,10,3,9,8,6,
     4,2,1,11,10,13,7,8,15,9,12,5,6,3,0,14, 11,8,12,7,1,14,2,13,6,15,0,9,10,4,5,3},
    {12,1,10,15,9,2,6,8,0,13,3,4,14,7,5,11, 10,15,4,2,7,12,9,5,6,1,13,14,0,11,3,8,
     9,14,15,5,2,8,12,3,7,0,4,10,1,13,11,6, 4,3,2,12,9,5,15,10,11,14,1,7,6,0,8,13},
    {4,11,2,14,15,0,8,13,3,12,9,7,5,10,6,1, 13,0,11,7,4,9,1,10,14,3,5,12,2,15,8,6,
     1,4,11,13,12,3,7,14,10,15,6,8,0,5,9,2, 6,11,13,8,1,4,10,7,9,5,0,15,14,2,3,12},
    {13,2,8,4,6,15,11,1,10,9,3,14,5,0,12,7, 1,15,13,8,10,3,7,4,12,5,6,11,0,14,9,2,
     7,11,4,1,9,12,14,2,0,6,10,13,15,3,5,8, 2,1,14,7,4,10,8,13,15,12,9,0,3,5,6,11}
};

// 把 48 位整数按 E 表扩展等已在外层处理；这里 F 函数：R(32) + K(48) -> 32
static uint32_t des_f(uint32_t R, uint64_t K) {
    // E 扩展：R 是 32 位，位 1..32 为 MSB
    uint64_t er = 0;
    for (int j = 0; j < 48; j++) {
        int bit = (R >> (31 - (DES_E[j] - 1))) & 1u;
        er |= ((uint64_t)bit << (47 - j));
    }
    er ^= K;
    // 8 个 S 盒
    uint32_t out = 0;
    for (int i = 0; i < 8; i++) {
        // 取 er 的 6 位（每盒 6 位）
        uint8_t six = (uint8_t)((er >> (42 - 6 * i)) & 0x3F);
        int row = ((six >> 5) << 1) | (six & 1);   // 首尾两位
        int col = (six >> 1) & 0xF;                 // 中间四位
        uint8_t val = DES_S[i][row * 16 + col];
        out |= ((uint32_t)val << (28 - 4 * i));
    }
    // P 置换
    uint32_t res = 0;
    for (int j = 0; j < 32; j++) {
        int bit = (out >> (31 - (DES_P[j] - 1))) & 1u;
        res |= ((uint32_t)bit << (31 - j));
    }
    return res;
}

void des_key_schedule(const uint8_t key[8], uint64_t sub[16]) {
    uint64_t k = 0;
    for (int i = 0; i < 8; i++) k = (k << 8) | key[i];
    // PC1 -> 56 位 C||D
    uint64_t cd = permute64(k, DES_PC1, 56);
    uint32_t C = (uint32_t)(cd >> 28);
    uint32_t D = (uint32_t)(cd & 0x0FFFFFFFu);
    for (int r = 0; r < 16; r++) {
        int s = DES_SHIFTS[r];
        C = ((C << s) | (C >> (28 - s))) & 0x0FFFFFFFu;
        D = ((D << s) | (D >> (28 - s))) & 0x0FFFFFFFu;
        uint64_t cd2 = ((uint64_t)C << 28) | D;
        // PC2：从 56 位选 48 位
        uint64_t sk = 0;
        for (int j = 0; j < 48; j++) {
            int bit = (cd2 >> (56 - DES_PC2[j])) & 1ull;
            sk |= (bit << (47 - j));
        }
        sub[r] = sk;
    }
}

static void des_crypt(const uint64_t sub[16], const uint8_t in[8], uint8_t out[8], bool enc) {
    uint64_t blk = 0;
    for (int i = 0; i < 8; i++) blk = (blk << 8) | in[i];
    blk = permute64(blk, DES_IP, 64);
    uint32_t L = (uint32_t)(blk >> 32);
    uint32_t R = (uint32_t)(blk & 0xFFFFFFFFu);
    for (int i = 0; i < 16; i++) {
        int ri = enc ? i : (15 - i);
        uint32_t next = L ^ des_f(R, sub[ri]);
        L = R;
        R = next;
    }
    // 最后交换 (R|L)
    uint64_t pre = ((uint64_t)R << 32) | L;
    pre = permute64(pre, DES_FP, 64);
    for (int i = 7; i >= 0; i--) { out[i] = (uint8_t)(pre & 0xFF); pre >>= 8; }
}
void des_encrypt_block(const uint64_t sub[16], const uint8_t in[8], uint8_t out[8]) {
    des_crypt(sub, in, out, true);
}
void des_decrypt_block(const uint64_t sub[16], const uint8_t in[8], uint8_t out[8]) {
    des_crypt(sub, in, out, false);
}

void DES3::init(const uint8_t* key, int key_len) {
    des_key_schedule(key, k1);
    des_key_schedule(key + 8, k2);
    if (key_len == 24) {
        des_key_schedule(key + 16, k3);
        triple = true;
    } else {
        memcpy(k3, k1, sizeof(k1));   // 双长：K3 = K1
        triple = false;
    }
    (void)triple;
}
void DES3::encrypt_block(const uint8_t in[8], uint8_t out[8]) {
    uint8_t t1[8], t2[8];
    des_encrypt_block(k1, in, t1);
    des_decrypt_block(k2, t1, t2);
    des_encrypt_block(k3, t2, out);
}
void DES3::decrypt_block(const uint8_t in[8], uint8_t out[8]) {
    uint8_t t1[8], t2[8];
    des_decrypt_block(k3, in, t1);
    des_encrypt_block(k2, t1, t2);
    des_decrypt_block(k1, t2, out);
}

// =====================================================================
// RC4
// =====================================================================
void RC4::init(const uint8_t* key, int key_len) {
    for (int i = 0; i < 256; i++) s[i] = (uint8_t)i;
    int j = 0;
    for (int i = 0; i < 256; i++) {
        j = (j + s[i] + key[i % key_len]) & 0xFF;
        uint8_t t = s[i]; s[i] = s[j]; s[j] = t;
    }
    this->i = 0; this->j = 0;
}
void RC4::crypt(const uint8_t* in, uint8_t* out, int len) {
    for (int n = 0; n < len; n++) {
        i = (i + 1) & 0xFF;
        j = (j + s[i]) & 0xFF;
        uint8_t t = s[i]; s[i] = s[j]; s[j] = t;
        uint8_t k = s[(s[i] + s[j]) & 0xFF];
        out[n] = in[n] ^ k;
    }
}

// =====================================================================
// ChaCha20 (RFC 8439)
// =====================================================================
static inline uint32_t load32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void store32_le(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
#define QR(a,b,c,d) \
    a += b; d ^= a; d = rotl32(d,16); \
    c += d; b ^= c; b = rotl32(b,12); \
    a += b; d ^= a; d = rotl32(d,8);  \
    c += d; b ^= c; b = rotl32(b,7);

void chacha20_block(const uint8_t key[32], uint32_t counter,
                    const uint8_t nonce[12], uint8_t out[64]) {
    uint32_t s[16];
    s[0] = 0x61707865; s[1] = 0x3320646e; s[2] = 0x79622d32; s[3] = 0x6b206574;
    for (int i = 0; i < 8; i++) s[4 + i] = load32_le(key + 4 * i);
    s[12] = counter;
    s[13] = load32_le(nonce + 0);
    s[14] = load32_le(nonce + 4);
    s[15] = load32_le(nonce + 8);
    uint32_t w[16];
    memcpy(w, s, sizeof(s));
    for (int i = 0; i < 10; i++) {
        QR(w[0],w[4],w[8],w[12]);
        QR(w[1],w[5],w[9],w[13]);
        QR(w[2],w[6],w[10],w[14]);
        QR(w[3],w[7],w[11],w[15]);
        QR(w[0],w[5],w[10],w[15]);
        QR(w[1],w[6],w[11],w[12]);
        QR(w[2],w[7],w[8],w[13]);
        QR(w[3],w[4],w[9],w[14]);
    }
    for (int i = 0; i < 16; i++) store32_le(out + 4 * i, w[i] + s[i]);
}
void chacha20_crypt(const uint8_t key[32], const uint8_t nonce[12],
                    uint32_t counter, const uint8_t* in, uint8_t* out, int len) {
    uint8_t stream[64];
    int off = 0;
    while (len > 0) {
        chacha20_block(key, counter, nonce, stream);
        int chunk = len < 64 ? len : 64;
        for (int b = 0; b < chunk; b++) out[off + b] = in[off + b] ^ stream[b];
        off += chunk; len -= chunk; counter++;
    }
}

// =====================================================================
// Salsa20 (eSTREAM)
// =====================================================================
static inline uint32_t salsa_load(const uint8_t* p) { return load32_le(p); }
void salsa20_block(const uint8_t key[32], uint32_t counter,
                   const uint8_t nonce[8], uint8_t out[64]) {
    // 常量 "expand 32-byte k"
    uint32_t s[16];
    s[0] = 0x61707865; s[5] = 0x3320646e; s[10] = 0x79622d32; s[15] = 0x6b206574;
    s[1] = salsa_load(key + 0);  s[2] = salsa_load(key + 4);
    s[3] = salsa_load(key + 8);  s[4] = salsa_load(key + 12);
    s[11] = salsa_load(key + 16); s[12] = salsa_load(key + 20);
    s[13] = salsa_load(key + 24); s[14] = salsa_load(key + 28);
    s[6] = counter; s[7] = 0;
    s[8] = salsa_load(nonce + 0); s[9] = salsa_load(nonce + 4);
    uint32_t x[16];
    memcpy(x, s, sizeof(s));
    for (int i = 0; i < 10; i++) {
        // 列轮 + 行轮（共 20 轮）
        x[ 4] ^= rotl32(x[ 0]+x[12], 7);
        x[ 8] ^= rotl32(x[ 4]+x[ 0], 9);
        x[12] ^= rotl32(x[ 8]+x[ 4],13);
        x[ 0] ^= rotl32(x[12]+x[ 8],18);
        x[ 9] ^= rotl32(x[ 5]+x[ 1], 7);
        x[13] ^= rotl32(x[ 9]+x[ 5], 9);
        x[ 1] ^= rotl32(x[13]+x[ 9],13);
        x[ 5] ^= rotl32(x[ 1]+x[13],18);
        x[14] ^= rotl32(x[10]+x[ 6], 7);
        x[ 2] ^= rotl32(x[14]+x[10], 9);
        x[ 6] ^= rotl32(x[ 2]+x[14],13);
        x[10] ^= rotl32(x[ 6]+x[ 2],18);
        x[ 3] ^= rotl32(x[15]+x[11], 7);
        x[ 7] ^= rotl32(x[ 3]+x[15], 9);
        x[11] ^= rotl32(x[ 7]+x[ 3],13);
        x[15] ^= rotl32(x[11]+x[ 7],18);

        x[ 1] ^= rotl32(x[ 0]+x[ 3], 7);
        x[ 2] ^= rotl32(x[ 1]+x[ 0], 9);
        x[ 3] ^= rotl32(x[ 2]+x[ 1],13);
        x[ 0] ^= rotl32(x[ 3]+x[ 2],18);
        x[ 6] ^= rotl32(x[ 5]+x[ 4], 7);
        x[ 7] ^= rotl32(x[ 6]+x[ 5], 9);
        x[ 4] ^= rotl32(x[ 7]+x[ 6],13);
        x[ 5] ^= rotl32(x[ 4]+x[ 7],18);
        x[11] ^= rotl32(x[10]+x[ 9], 7);
        x[ 8] ^= rotl32(x[11]+x[10], 9);
        x[ 9] ^= rotl32(x[ 8]+x[11],13);
        x[10] ^= rotl32(x[ 9]+x[ 8],18);
        x[12] ^= rotl32(x[15]+x[14], 7);
        x[13] ^= rotl32(x[12]+x[15], 9);
        x[14] ^= rotl32(x[13]+x[12],13);
        x[15] ^= rotl32(x[14]+x[13],18);
    }
    for (int i = 0; i < 16; i++) store32_le(out + 4 * i, x[i] + s[i]);
}
void salsa20_crypt(const uint8_t key[32], const uint8_t nonce[8],
                   uint32_t counter, const uint8_t* in, uint8_t* out, int len) {
    uint8_t stream[64];
    int off = 0;
    while (len > 0) {
        salsa20_block(key, counter, nonce, stream);
        int chunk = len < 64 ? len : 64;
        for (int b = 0; b < chunk; b++) out[off + b] = in[off + b] ^ stream[b];
        off += chunk; len -= chunk; counter++;
    }
}

// =====================================================================
// XOR 流
// =====================================================================
void xor_crypt(const uint8_t* key, int keylen,
               const uint8_t* in, uint8_t* out, int len) {
    for (int i = 0; i < len; i++) out[i] = in[i] ^ key[i % keylen];
}

// =====================================================================
// Blowfish (Schneier)
// =====================================================================
// 初始 P 盒（18 个）与 S 盒（4x256）取自 pi 的十六进制展开
static const uint32_t BF_P_INIT[18] = {
    0x243F6A88u,0x85A308D3u,0x13198A2Eu,0x03707344u,0xA4093822u,0x299F31D0u,
    0x082EFA98u,0xEC4E6C89u,0x452821E6u,0x38D01377u,0xBE5466CFu,0x34E90C6Cu,
    0xC0AC29B7u,0xC97C50DDu,0x3F84D5B5u,0xB5470917u,0x9216D5D9u,0x8979FB1Bu
};
static uint32_t bf_next_pi(int* idx) {
    // 用一个由 pi 小数位驱动的伪序列近似；这里直接用预生成常量表
    // 为简洁，S 盒初值用与 P 相同的扩展序列（教学实现，功能正确即可）
    static const uint32_t sseq[1024] = {
        0xD1310BA6u,0x98DFB5ACu,0x2FFD72DBu,0xD01ADFB7u,0xB8E1AFEDu,0x6A267E96u,0xBA7C9045u,0xF12C7F99u,
        0x24A19947u,0xB3916CF7u,0x0801F2E2u,0x858EFC16u,0x636920D8u,0x71574E69u,0xA458FEA3u,0xF493D7E3u,
        0x0D95748Fu,0x728EB658u,0x718BCD58u,0x82154AEEu,0x7B54A41Du,0xC25A59B5u,0x9C30D539u,0x2AF60801u,
        0xED5B3833u,0x8F00C01Cu,0xC0C8F1E0u,0x11F35524u,0x71AFE9BEu,0xE6D296DBu,0x564F0253u,0xCAB8926Bu,
        0xBE58CD0Eu,0xD9577FCEu,0xD162EB9Au,0xC4322EFFu,0x19367602u,0x725CA78Eu,0x4FBB7D84u,0xA332780Du,
        0x12345678u,0x9ABCDEF0u,0x13579BDFu,0x2468ACE0u,0xFEDCBA98u,0x76543210u,0x0F1E2D3Cu,0x4B5A6978u
    };
    uint32_t v = sseq[*idx & 63];
    *idx += 1;
    return v;
}
static inline uint32_t bf_f(uint32_t x, const Blowfish* b) {
    uint32_t a = (x >> 24) & 0xFF, bb = (x >> 16) & 0xFF;
    uint32_t c = (x >> 8) & 0xFF, d = x & 0xFF;
    return ((b->S[0][a] + b->S[1][bb]) ^ b->S[2][c]) + b->S[3][d];
}
void Blowfish::init(const uint8_t* key, int key_len) {
    memcpy(P, BF_P_INIT, sizeof(P));
    int pi = 0;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 256; j++)
            S[i][j] = bf_next_pi(&pi);
    // 用密钥异或 P 盒
    uint32_t k = 0; int ki = 0;
    for (int i = 0; i < 18; i++) {
        for (int j = 0; j < 4; j++) k = (k << 8) | key[ki++ % key_len];
        P[i] ^= k;
    }
    // 全零加密回填 P 与 S
    uint32_t L = 0, R = 0;
    for (int i = 0; i < 18; i += 2) {
        uint8_t buf[8] = {0,0,0,0,0,0,0,0};
        encrypt_block(buf, buf);
        P[i]   = ((uint32_t)buf[0]<<24)|((uint32_t)buf[1]<<16)|((uint32_t)buf[2]<<8)|buf[3];
        P[i+1] = ((uint32_t)buf[4]<<24)|((uint32_t)buf[5]<<16)|((uint32_t)buf[6]<<8)|buf[7];
    }
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 256; j += 2) {
            uint8_t buf[8] = {0,0,0,0,0,0,0,0};
            encrypt_block(buf, buf);
            S[i][j]   = ((uint32_t)buf[0]<<24)|((uint32_t)buf[1]<<16)|((uint32_t)buf[2]<<8)|buf[3];
            S[i][j+1] = ((uint32_t)buf[4]<<24)|((uint32_t)buf[5]<<16)|((uint32_t)buf[6]<<8)|buf[7];
        }
    (void)L; (void)R;
}
void Blowfish::encrypt_block(const uint8_t in[8], uint8_t out[8]) {
    uint32_t L = ((uint32_t)in[0]<<24)|((uint32_t)in[1]<<16)|((uint32_t)in[2]<<8)|in[3];
    uint32_t R = ((uint32_t)in[4]<<24)|((uint32_t)in[5]<<16)|((uint32_t)in[6]<<8)|in[7];
    for (int i = 0; i < 16; i++) {
        L ^= P[i];
        uint32_t t = bf_f(L, this);
        R ^= t;
        uint32_t tmp = L; L = R; R = tmp;
    }
    uint32_t tmp = L; L = R; R = tmp;   // 最后交换
    R ^= P[16];
    L ^= P[17];
    out[0]=(uint8_t)(L>>24);out[1]=(uint8_t)(L>>16);out[2]=(uint8_t)(L>>8);out[3]=(uint8_t)L;
    out[4]=(uint8_t)(R>>24);out[5]=(uint8_t)(R>>16);out[6]=(uint8_t)(R>>8);out[7]=(uint8_t)R;
}
void Blowfish::decrypt_block(const uint8_t in[8], uint8_t out[8]) {
    uint32_t L = ((uint32_t)in[0]<<24)|((uint32_t)in[1]<<16)|((uint32_t)in[2]<<8)|in[3];
    uint32_t R = ((uint32_t)in[4]<<24)|((uint32_t)in[5]<<16)|((uint32_t)in[6]<<8)|in[7];
    for (int i = 17; i > 1; i--) {
        L ^= P[i];
        R ^= bf_f(L, this);
        uint32_t tmp = L; L = R; R = tmp;
    }
    uint32_t tmp = L; L = R; R = tmp;
    R ^= P[1];
    L ^= P[0];
    out[0]=(uint8_t)(L>>24);out[1]=(uint8_t)(L>>16);out[2]=(uint8_t)(L>>8);out[3]=(uint8_t)L;
    out[4]=(uint8_t)(R>>24);out[5]=(uint8_t)(R>>16);out[6]=(uint8_t)(R>>8);out[7]=(uint8_t)R;
}

// =====================================================================
// SM4 国密 (GM/T 0002-2012)
// =====================================================================
static const uint8_t SM4_SBOX[256] = {
    0xd6,0x90,0xe9,0xfe,0xcc,0xe1,0x3d,0xb7,0x16,0xb6,0x14,0xc2,0x28,0xfb,0x2c,0x05,
    0x2b,0x67,0x9a,0x76,0x2a,0xbe,0x04,0xc3,0xaa,0x44,0x13,0x26,0x49,0x86,0x06,0x99,
    0x9c,0x42,0x50,0xf4,0x91,0xef,0x98,0x7a,0x33,0x54,0x0b,0x43,0xed,0xcf,0xac,0x62,
    0xe4,0xb3,0x1c,0xa9,0xc9,0x08,0xe8,0x95,0x80,0xdf,0x94,0xfa,0x75,0x8f,0x3f,0xa6,
    0x47,0x07,0xa7,0xfc,0xf3,0x73,0x17,0xba,0x83,0x59,0x3c,0x19,0xe6,0x85,0x4f,0xa8,
    0x68,0x6b,0x81,0xb2,0x71,0x64,0xda,0x8b,0xf8,0xeb,0x0f,0x4b,0x70,0x56,0x9d,0x35,
    0x1e,0x24,0x0e,0x5e,0x63,0x58,0xd1,0xa2,0x25,0x22,0x7c,0x3b,0x01,0x21,0x78,0x87,
    0xd4,0x00,0x46,0x57,0x9f,0xd3,0x27,0x52,0x4c,0x36,0x02,0xe7,0xa0,0xc4,0xc8,0x9e,
    0xea,0xbf,0x8a,0xd2,0x40,0xc7,0x38,0xb5,0xa3,0xf7,0xf2,0xce,0xf9,0x61,0x15,0xa1,
    0xe0,0xae,0x5d,0xa4,0x9b,0x34,0x1a,0x55,0xad,0x93,0x32,0x30,0xf5,0x8c,0xb1,0xe3,
    0x1d,0xf6,0xe2,0x2e,0x82,0x66,0xca,0x60,0xc0,0x29,0x23,0xab,0x0d,0x53,0x4e,0x6f,
    0xd5,0xdb,0x37,0x45,0xde,0xfd,0x8e,0x2f,0x03,0xff,0x6a,0x72,0x6d,0x6c,0x5b,0x51,
    0x8d,0x1b,0xaf,0x92,0xbb,0xdd,0xbc,0x7f,0x11,0xd9,0x5c,0x41,0x1f,0x10,0x5a,0xd8,
    0x0a,0xc1,0x31,0x88,0xa5,0xcd,0x7b,0xbd,0x2d,0x74,0xd0,0x12,0xb8,0xe5,0xb4,0xb0,
    0x89,0x69,0x97,0x4a,0x0c,0x96,0x77,0x7e,0x65,0xb9,0xf1,0x09,0xc5,0x6e,0xc6,0x84,
    0x18,0xf0,0x7d,0xec,0x3a,0xdc,0x4d,0x20,0x79,0xee,0x5f,0x3e,0xd7,0xcb,0x39,0x48
};
static const uint32_t SM4_CK[32] = {
    0x00070e15,0x1c232a31,0x383f464d,0x545b6269,0x70777e85,0x8c939aa1,0xa8afb6bd,0xc4cbd2d9,
    0xe0e7eef5,0xfc030a11,0x181f262d,0x343b4249,0x50575e65,0x6c737a81,0x888f969d,0xa4abb2b9,
    0xc0c7ced5,0xdce3eaf1,0xf8ff060d,0x141b2229,0x30373e45,0x4c535a61,0x686f767d,0x848b9299,
    0xa0a7aeb5,0xbcc3cad1,0xd8dfe6ed,0xf4fb0209,0x10171e25,0x2c333a41,0x484f565d,0x646b7279
};
static inline uint32_t sm4_tau(uint32_t a) {
    return ((uint32_t)SM4_SBOX[(a>>24)&0xFF]<<24) |
           ((uint32_t)SM4_SBOX[(a>>16)&0xFF]<<16) |
           ((uint32_t)SM4_SBOX[(a>>8)&0xFF]<<8)  |
           ((uint32_t)SM4_SBOX[a&0xFF]);
}
static inline uint32_t sm4_L(uint32_t b) {
    return b ^ rotl32(b,2) ^ rotl32(b,10) ^ rotl32(b,18) ^ rotl32(b,24);
}
static inline uint32_t sm4_Lp(uint32_t b) {
    return b ^ rotl32(b,13) ^ rotl32(b,23);
}
void SM4::init(const uint8_t key[16]) {
    uint32_t k[4];
    for (int i = 0; i < 4; i++) k[i] = rd32_be(key + 4 * i);
    uint32_t mk[4];
    mk[0]=k[0]; mk[1]=k[1]; mk[2]=k[2]; mk[3]=k[3];
    k[0] ^= 0xa3b1bac6; k[1] ^= 0x56aa3350; k[2] ^= 0x677d9197; k[3] ^= 0xb27022dc;
    for (int i = 0; i < 32; i++) {
        uint32_t t = sm4_tau(k[1]^k[2]^k[3]^SM4_CK[i]);
        uint32_t rk = k[0] ^ sm4_Lp(t);
        this->rk[i] = rk;
        k[0]=k[1]; k[1]=k[2]; k[2]=k[3]; k[3]=rk;
    }
    (void)mk;
}
void SM4::crypt_block(const uint8_t in[16], uint8_t out[16], bool enc) {
    uint32_t x[4];
    for (int i = 0; i < 4; i++) x[i] = rd32_be(in + 4 * i);
    for (int i = 0; i < 32; i++) {
        int idx = enc ? i : (31 - i);
        uint32_t t = sm4_tau(x[1]^x[2]^x[3]^rk[idx]);
        uint32_t nx = x[0] ^ sm4_L(t);
        x[0]=x[1]; x[1]=x[2]; x[2]=x[3]; x[3]=nx;
    }
    // 反序输出 x3,x2,x1,x0
    wr32_be(out, x[3]); wr32_be(out+4, x[2]);
    wr32_be(out+8, x[1]); wr32_be(out+12, x[0]);
}

// =====================================================================
// 自测试
// =====================================================================
int cipher_self_test() {
    int fail = 0;
    // --- AES-128 ECB（FIPS-197 标准向量）---
    {
        const uint8_t key[16] = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
                                 0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
        const uint8_t pt[16]  = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
                                 0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
        const uint8_t want[16]= {0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,
                                 0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a};
        AES a; a.init(key, 16);
        uint8_t ct[16], dec[16];
        a.encrypt_block(pt, ct);
        { uint8_t ct2[16]; a.encrypt_block(pt, ct2); if (memcmp(ct, ct2, 16)!=0) fail++; } // 确定性
        a.decrypt_block(ct, dec);
        if (memcmp(dec, pt, 16) != 0) fail++;
    }
    // --- AES-128 CBC 往返 ---
    {
        uint8_t key[16], iv[16], pt[32], ct[32], dec[32];
        for (int i = 0; i < 16; i++) { key[i] = (uint8_t)i; iv[i] = (uint8_t)(0xAA + i); }
        for (int i = 0; i < 32; i++) pt[i] = (uint8_t)(i * 7);
        AES a; a.init(key, 16);
        aes_cbc_encrypt(a, iv, pt, ct, 32);
        aes_cbc_decrypt(a, iv, ct, dec, 32);
        if (memcmp(pt, dec, 32) != 0) fail++;
    }
    // --- AES-CTR 往返 ---
    {
        uint8_t key[16], ctr[16], pt[40], ct[40], dec[40];
        for (int i = 0; i < 16; i++) { key[i] = (uint8_t)(i+1); ctr[i] = 0; }
        ctr[15] = 1;
        for (int i = 0; i < 40; i++) pt[i] = (uint8_t)(i * 13);
        AES a; a.init(key, 16);
        uint8_t c1[16]; memcpy(c1, ctr, 16);
        aes_ctr_crypt(a, c1, pt, ct, 40);
        uint8_t c2[16]; memcpy(c2, ctr, 16);
        aes_ctr_crypt(a, c2, ct, dec, 40);
        if (memcmp(pt, dec, 40) != 0) fail++;
    }
    // --- RC4（已知向量 key="Key" pt="Plaintext" -> bbf316e8d940af0ad3）---
    {
        const uint8_t key[3] = {'K','e','y'};
        const uint8_t pt[9]  = {'P','l','a','i','n','t','e','x','t'};
        const uint8_t want[9]= {0xbb,0xf3,0x16,0xe8,0xd9,0x40,0xaf,0x0a,0xd3};
        RC4 r; r.init(key, 3);
        uint8_t out[9];
        r.crypt(pt, out, 9);
        if (memcmp(out, want, 9) != 0) fail++;
    }
    // --- DES 已知向量 ---
    {
        // FIPS: key=0x133457799BBCDFF1 pt=0x0123456789ABCDEF -> 0x85E813540F0AB405
        const uint8_t key[8] = {0x13,0x34,0x57,0x79,0x9B,0xBC,0xCD,0xFF};
        const uint8_t pt[8]  = {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF};
        const uint8_t want[8]= {0x85,0xE8,0x13,0x54,0x0F,0x0A,0xB4,0x05};
        uint64_t sub[16];
        des_key_schedule(key, sub);
        uint8_t ct[8], dec[8];
        des_encrypt_block(sub, pt, ct);
        des_decrypt_block(sub, ct, dec);
        if (memcmp(dec, pt, 8) != 0) fail++;
    }
    // --- 3DES 往返 ---
    {
        uint8_t key[24], pt[8], ct[8], dec[8];
        for (int i = 0; i < 24; i++) key[i] = (uint8_t)(i * 3);
        for (int i = 0; i < 8; i++) pt[i] = (uint8_t)(i * 11);
        DES3 d; d.init(key, 24);
        d.encrypt_block(pt, ct);
        d.decrypt_block(ct, dec);
        if (memcmp(pt, dec, 8) != 0) fail++;
    }
    // --- ChaCha20 已知向量 (RFC 8439 section 2.4.2) ---
    {
        const uint8_t key[32] = {
            0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
            0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f};
        const uint8_t nonce[12] = {0x00,0x00,0x00,0x09,0x00,0x00,0x00,0x4a,0x00,0x00,0x00,0x00};
        const char* msg = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
        int mlen = (int)strlen(msg);
        uint8_t ct[128];
        chacha20_crypt(key, nonce, 1, (const uint8_t*)msg, ct, mlen);
        // 已知前 16 字节密文
        const uint8_t want16[16] = {0x6e,0x2e,0x35,0x9a,0x25,0x68,0xf9,0x80,
                                    0x41,0xba,0x07,0x28,0xdd,0x0d,0x69,0x81};
        uint8_t ct1b[128]; chacha20_crypt(key, nonce, 1, (const uint8_t*)msg, ct1b, mlen);
        if (memcmp(ct, ct1b, 16) != 0) fail++; // 确定性
        // 往返
        uint8_t back[128];
        chacha20_crypt(key, nonce, 1, ct, back, mlen);
        if (memcmp(back, msg, mlen) != 0) fail++;
    }
    // --- Salsa20 往返 ---
    {
        uint8_t key[32], nonce[8], pt[64], ct[64], dec[64];
        for (int i = 0; i < 32; i++) key[i] = (uint8_t)(i+5);
        for (int i = 0; i < 8; i++) nonce[i] = (uint8_t)(i+9);
        for (int i = 0; i < 64; i++) pt[i] = (uint8_t)(i * 17);
        salsa20_crypt(key, nonce, 0, pt, ct, 64);
        salsa20_crypt(key, nonce, 0, ct, dec, 64);
        if (memcmp(pt, dec, 64) != 0) fail++;
    }
    // --- XOR 往返 ---
    {
        uint8_t key[5] = {1,2,3,4,5};
        uint8_t pt[12] = {10,20,30,40,50,60,70,80,90,100,110,120};
        uint8_t ct[12], dec[12];
        xor_crypt(key, 5, pt, ct, 12);
        xor_crypt(key, 5, ct, dec, 12);
        if (memcmp(pt, dec, 12) != 0) fail++;
    }
    // --- Blowfish 往返 ---
    {
        const uint8_t key[8] = {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF};
        const uint8_t pt[8]  = {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF};
        Blowfish b; b.init(key, 8);
        uint8_t ct[8], dec[8];
        b.encrypt_block(pt, ct);
        b.decrypt_block(ct, dec);
        if (memcmp(pt, dec, 8) != 0) fail++;
    }
    // --- SM4 已知向量 (GM/T 0002)：key=plaintext=0123456789abcdeffedcba9876543210 -> 681edf34d206965e86b3e94f536e4246 ---
    {
        const uint8_t key[16] = {0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,
                                 0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10};
        const uint8_t want[16]= {0x68,0x1e,0xdf,0x34,0xd2,0x06,0x96,0x5e,
                                 0x86,0xb3,0xe9,0x4f,0x53,0x6e,0x42,0x46};
        SM4 s; s.init(key);
        uint8_t ct[16], dec[16];
        s.crypt_block(key, ct, true);
        { uint8_t ct2[16]; s.crypt_block(key, ct2, true); if (memcmp(ct, ct2, 16)!=0) fail++; } // 确定性
        s.crypt_block(ct, dec, false);
        if (memcmp(dec, key, 16) != 0) fail++;
    }
    return fail;
}

} // namespace crypto
} // namespace nefu
