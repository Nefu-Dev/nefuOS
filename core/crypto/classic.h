// nefuOS 密码学库 —— 古典密码模块
// 教学用途，全部基于字母 A-Z（自动转大写，忽略非字母）：
//   Caesar、Vigenere、Autokey、Beaufort、Playfair、Hill、Bifid、Trifid、
//   Four-square、单表替换、栅栏(rail fence)、列置换、一次一密、Enigma 简化版。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace crypto {

// ---- Caesar：移位 shift（0..25）----
void caesar_encrypt(const char* in, int shift, char* out);
void caesar_decrypt(const char* in, int shift, char* out);

// ---- Vigenere：关键字重复 ----
void vigenere_encrypt(const char* in, const char* key, char* out);
void vigenere_decrypt(const char* in, const char* key, char* out);

// ---- Autokey：密钥后接明文流 ----
void autokey_encrypt(const char* in, const char* key, char* out);
void autokey_decrypt(const char* in, const char* key, char* out);

// ---- Beaufort：key - cipher（与 Vigenere 方向相反，自反）----
void beaufort_crypt(const char* in, const char* key, char* out);

// ---- Playfair：5x5 矩阵（I/J 合并），key 为字母关键字 ----
void playfair_encrypt(const char* key, const char* in, char* out);
void playfair_decrypt(const char* key, const char* in, char* out);

// ---- Hill：2x2 矩阵（key 为 4 个字母）----
void hill_encrypt(const char* key2x2, const char* in, char* out);
void hill_decrypt(const char* key2x2, const char* in, char* out);

// ---- Bifid：5x5 Polybius ----
void bifid_encrypt(const char* key, const char* in, char* out);
void bifid_decrypt(const char* key, const char* in, char* out);

// ---- Trifid：3x3x3 立方体 ----
void trifid_encrypt(const char* key, const char* in, char* out);
void trifid_decrypt(const char* key, const char* in, char* out);

// ---- Four-square：两个关键字矩阵 ----
void foursquare_encrypt(const char* key1, const char* key2, const char* in, char* out);
void foursquare_decrypt(const char* key1, const char* key2, const char* in, char* out);

// ---- 单表替换：key 为 26 字母置换 ----
void substitution_encrypt(const char* key26, const char* in, char* out);
void substitution_decrypt(const char* key26, const char* in, char* out);

// ---- 栅栏密码 rail fence（rails>=2）----
void railfence_encrypt(const char* in, int rails, char* out);
void railfence_decrypt(const char* in, int rails, char* out);

// ---- 列置换：key 给出列顺序 ----
void columnar_encrypt(const char* key, const char* in, char* out);
void columnar_decrypt(const char* key, const char* in, char* out);

// ---- 一次一密：key 与明文等长 ----
void onetimepad_crypt(const char* in, const char* key, char* out);

// ---- Enigma 简化版（3 转子 + 反射器）----
struct Enigma {
    int pos[3];                 // 转子位置 0..25
    int ring[3];                // 环设置
    void reset(int r1,int r2,int r3);
    int  rotor(int c, int which, bool forward);
    int  reflect(int c);
    char step(char c);
    void encrypt(const char* in, char* out);
};

// 自测试：返回失败数（0 全部通过）
int classic_self_test();

} // namespace crypto
} // namespace nefu
