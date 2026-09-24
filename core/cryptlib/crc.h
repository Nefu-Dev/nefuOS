// nefuOS crypto library — CRC (crc)
// CRC：循环冗余校验。把数据视为多项式，模 2 除法求余。
// CRC32（IEEE 802.3，多项式 0xEDB88320，初始 0xFFFFFFFF，异或输出）
// 用于 zip/gzip/以太网帧校验；CRC64 用于大文件/数据库校验。
// 本实现用"查表法"（预计算 256 项表，按字节处理），教学注释含手工除法原理。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace crypt {

// CRC32（返回 0..0xFFFFFFFF）
uint32_t crc32(const unsigned char* data, int n);
// CRC32 字符串便捷版
uint32_t crc32_str(const char* s);
// CRC64（ECMA-182 多项式 0xC96C5795D7870F42，初始 0xFFFFFFFFFFFFFFFF）
uint64_t crc64(const unsigned char* data, int n);
uint64_t crc64_str(const char* s);

int crc_self_test();

} // namespace crypt
} // namespace nefu
