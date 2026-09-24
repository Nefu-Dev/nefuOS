// nefuOS 压缩算法库 —— LZ77 家族字典压缩
//
// 实现六个经典字典/滑动窗口算法：
//
//   1. LZ77（lz77_*）
//      在"滑动窗口"里找最长重复串，输出 (距离, 长度) 对或字面字节。
//      用 3 字节哈希加速匹配，窗口 32KB。是 gzip/DEFLATE 的鼻祖。
//
//   2. LZSS（lzss_*）
//      LZ77 的存储友好变体：每 8 个 token 共用一个标志字节位域，
//      字面量直接跟在后面，匹配用 (u16 距离, u8 长度)。
//
//   3. LZ78（lz78_*）
//      不存窗口，而是维护"短语字典"，输出 (字典索引, 下一字符)。
//
//   4. LZW（lzw_*）
//      LZ78 的变种：编码器只输出"字典索引"，解码器据此前推重建短语。
//      12 位码、4096 项字典、字典满后冻结。Unix compress / GIF 用它。
//
//   5. LZRW1（lzrw1_*）
//      Ross Williams 的快速算法：4KB 窗口、18 位包格式，
//      极快、压缩率中等，教学版简化实现。
//
//   6. LZP（lzp_*）
//      预测式：用上一次出现位置做预测，命中就只发一个"命中"标记，
//      否则发字面字节。对高度重复数据极高效。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace compress {

int lz77_encode(const uint8_t* in, int n, uint8_t* out, int out_cap);
int lz77_decode(const uint8_t* in, int n, uint8_t* out, int out_cap);

int lzss_encode(const uint8_t* in, int n, uint8_t* out, int out_cap);
int lzss_decode(const uint8_t* in, int n, uint8_t* out, int out_cap);

int lz78_encode(const uint8_t* in, int n, uint8_t* out, int out_cap);
int lz78_decode(const uint8_t* in, int n, uint8_t* out, int out_cap);

int lzw_encode(const uint8_t* in, int n, uint8_t* out, int out_cap);
int lzw_decode(const uint8_t* in, int n, uint8_t* out, int out_cap);

int lzrw1_encode(const uint8_t* in, int n, uint8_t* out, int out_cap);
int lzrw1_decode(const uint8_t* in, int n, uint8_t* out, int out_cap);

int lzp_encode(const uint8_t* in, int n, uint8_t* out, int out_cap);
int lzp_decode(const uint8_t* in, int n, uint8_t* out, int out_cap);

int lz_self_test();

} // namespace compress
} // namespace nefu
