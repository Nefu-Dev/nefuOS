// nefuOS crypto library — SHA-256 (sha256)
// SHA-256：安全散列算法，输出 256 位（32 字节）摘要。
// 经典流程：填充（追加 0x80 + 补零 + 64 位长度）→ 按 512 位分块 →
// 每块展开 64 个字 → 64 轮压缩（四个工作变量 + 8 个初值）。
// 应用：文件校验、密码存储（加盐后）、数字签名、区块链 Merkle 根。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace crypt {

// 计算 sha256 摘要：输入 data[0..n-1]，输出 32 字节到 out
void sha256(const unsigned char* data, int n, unsigned char out[32]);
// 便捷：对 C 字符串求 sha256
void sha256_str(const char* s, unsigned char out[32]);
// 十六进制输出（out32 的 hex 写入 hexbuf，至少 65 字节）
void sha256_hex(const unsigned char* out32, char* hexbuf);

int sha256_self_test();

} // namespace crypt
} // namespace nefu
