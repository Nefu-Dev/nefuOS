// nefuOS crypto library — Base64/Base32 (b64)
// Base64：把 3 字节（24 位）拆成 4 个 6 位组，映射到 A-Za-z0-9+/。
// 不足 3 字节时补 '='。用途：邮件附件（MIME）、URL 安全传输、JSON
// 二进制嵌入。附带 Base32（RFC 4648，32 字符字母表，5 位一组）。
#pragma once
#include <stddef.h>

namespace nefu {
namespace crypt {

// 编码：输出写到 out（大小 >= (n+2)/3*4 + 1）；返回输出长度（不含 '\0'）
int base64_encode(const unsigned char* data, int n, char* out);
// 解码：输出写到 out（大小 >= n/4*3）；返回输出长度；非法字符返回 -1
int base64_decode(const char* in, int n, unsigned char* out);
// 便捷字符串版（编码后自动补 '\0'）
void base64_str(const char* s, char* out);

// Base32（RFC 4648 标准字母表 A-Z2-7）
int base32_encode(const unsigned char* data, int n, char* out);
int base32_decode(const char* in, int n, unsigned char* out);

int b64_self_test();

} // namespace crypt
} // namespace nefu
