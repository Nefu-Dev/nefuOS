// nefuOS crypto library — XOR 流密码 & 经典变换 (xor)
// XOR 流密码：最简单的"一次性密码本"变体——密钥字节与明文逐位异或。
// 安全性完全依赖密钥（教学演示；真实场景用 CSPRNG 密钥 + 不重用）。
// 附 ROT13（凯撒密码位移 13，仅字母）与简单 Vigenère 密码，
// 三个经典变换一起演示"替换式古典密码 → 现代流密码"的演化。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace crypt {

// XOR 流密码：密钥循环使用（教学）；加解密同一函数
void xor_cipher(const unsigned char* key, int klen, unsigned char* data, int n);
// ROT13：字母位移 13（仅 ASCII 字母，其他字符不变）；两次调用还原
void rot13(char* s);
// Vigenère：密钥字母位移循环（仅 A-Z/a-z）；mode 0=加密 1=解密
void vigenere(const char* key, char* text, bool decrypt);
// 字符频率统计：返回 text 中英文字母总数（用于频率分析教学）
int letter_count(const char* text);

int xor_self_test();

} // namespace crypt
} // namespace nefu
