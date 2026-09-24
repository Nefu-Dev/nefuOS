// nefuOS 密码学库 —— 编码/解码 (codec) 模块
// 覆盖各种二进制<->文本编码：Base32、Base36、Base58、Base62、
// Base85(Ascii85/Z85)、Base91、hex、ROT13/ROT47、URL 编码、quoted-printable。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace crypto {

// 返回值：编码后的字符串长度（不含 NUL），或解码输出字节数；负数表示错误。

// ---- Base32 (RFC 4648，字母表 A-Z2-7，补 '=') ----
int base32_encode(const uint8_t* in, int len, char* out, int outcap);
int base32_decode(const char* in, uint8_t* out, int outcap);

// ---- Base36（0-9 a-z）----
int base36_encode(const uint8_t* in, int len, char* out, int outcap);
int base36_decode(const char* in, uint8_t* out, int outcap);

// ---- Base58（Bitcoin 字母表，无 0/O/I/l）----
int base58_encode(const uint8_t* in, int len, char* out, int outcap);
int base58_decode(const char* in, uint8_t* out, int outcap);

// ---- Base62（0-9 A-Z a-z）----
int base62_encode(const uint8_t* in, int len, char* out, int outcap);
int base62_decode(const char* in, uint8_t* out, int outcap);

// ---- Base85：Ascii85（<~ ... ~>，补 z 缩写）与 Z85（ZeroMQ）----
int base85_ascii85_encode(const uint8_t* in, int len, char* out, int outcap);
int base85_ascii85_decode(const char* in, uint8_t* out, int outcap);
int z85_encode(const uint32_t* in_groups, int ngroups, char* out, int outcap);

// ---- Base91 ----
int base91_encode(const uint8_t* in, int len, char* out, int outcap);
int base91_decode(const char* in, uint8_t* out, int outcap);

// ---- hex ----
void hex_encode(const uint8_t* in, int len, char* out);
int  hex_decode(const char* in, uint8_t* out, int outcap);

// ---- ROT13 / ROT47 ----
void rot13(const char* in, char* out);
void rot47(const char* in, char* out);

// ---- URL 编码（百分号编码）----
void url_encode(const char* in, char* out, int outcap);
void url_decode(const char* in, char* out, int outcap);

// ---- quoted-printable (RFC 2045) ----
void qp_encode(const char* in, char* out, int outcap);
void qp_decode(const char* in, char* out, int outcap);

// 自测试：返回失败数（0 全部通过）
int codec_self_test();

} // namespace crypto
} // namespace nefu
