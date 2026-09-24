// nefuOS crypto library — XOR/ROT13/Vigenère implementation
#include "xor.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace crypt {

void xor_cipher(const unsigned char* key, int klen, unsigned char* data, int n) {
    if (klen <= 0) return;
    for (int i = 0; i < n; i++) data[i] ^= key[i % klen];
}

void rot13(char* s) {
    for (int i = 0; s[i]; i++) {
        char c = s[i];
        if (c >= 'a' && c <= 'z') s[i] = (char)('a' + (c - 'a' + 13) % 26);
        else if (c >= 'A' && c <= 'Z') s[i] = (char)('A' + (c - 'A' + 13) % 26);
    }
}

void vigenere(const char* key, char* text, bool decrypt) {
    int klen = 0;
    while (key[klen]) klen++;
    if (klen == 0) return;
    int ki = 0;
    for (int i = 0; text[i]; i++) {
        char c = text[i];
        int base = 0;
        if (c >= 'a' && c <= 'z') base = 'a';
        else if (c >= 'A' && c <= 'Z') base = 'A';
        else continue;                    // 非字母不处理（但推进密钥？古典实现推进）
        char kc = key[ki % klen];
        int kbase = (kc >= 'a' && kc <= 'z') ? 'a' : 'A';
        int shift = (kc >= 'a' && kc <= 'z') ? kc - 'a' : kc - 'A';
        if (decrypt) shift = (26 - shift % 26) % 26;
        text[i] = (char)(base + (c - base + shift) % 26);
        ki++;
    }
}

int letter_count(const char* text) {
    int c = 0;
    for (int i = 0; text[i]; i++) {
        char ch = text[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) c++;
    }
    return c;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int xor_self_test() {
    g_fails = 0;
    // XOR 对称往返
    {
        unsigned char key[] = "9f3kq";
        unsigned char msg[] = "attack at dawn!";
        int n = (int)strlen((const char*)msg);
        unsigned char enc[64], dec[64];
        memcpy(enc, msg, n);
        xor_cipher(key, 5, enc, n);
        memcpy(dec, enc, n);
        xor_cipher(key, 5, dec, n);
        expect("xor-roundtrip", memcmp(dec, msg, n) == 0);
    }
    // 单字节异或探测（古典"破译"练习的基础）
    {
        // 已知明文 "hello"，单字节密钥 k：密文 = hello 每个字节 ^ k
        unsigned char k = 0x2A;
        unsigned char msg[] = "hello";
        xor_cipher(&k, 1, msg, 5);
        expect("xor-single-key", msg[0] == (unsigned char)('h' ^ 0x2A));
    }
    // ROT13：两次调用还原
    {
        char s[] = "Hello, nefuOS!";
        rot13(s);
        expect("rot13-enc", strcmp(s, "Uryyb, arshBF!") == 0);
        rot13(s);
        expect("rot13-roundtrip", strcmp(s, "Hello, nefuOS!") == 0);
    }
    // Vigenère：经典向量 "ATTACKATDAWN" / "LEMON" → "LXFOPVEFRNHR"
    {
        char s[] = "ATTACKATDAWN";
        vigenere("LEMON", s, false);
        expect("vig-enc", strcmp(s, "LXFOPVEFRNHR") == 0);
        vigenere("LEMON", s, true);
        expect("vig-dec", strcmp(s, "ATTACKATDAWN") == 0);
    }
    // letter_count
    {
        expect("lc-basic", letter_count("Hello 123 World!") == 10);
        expect("lc-empty", letter_count("!!!") == 0);
    }
    return g_fails;
}

} // namespace crypt
} // namespace nefu
