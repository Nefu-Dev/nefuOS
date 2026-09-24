// nefuOS 压缩算法库 —— Huffman 编码模块
//
// 实现三种经典 Huffman：
//
//   1. 静态 Huffman（huffman_static_*）
//      先统计整段数据的字节频率，建最优二叉树，再按规范码字（canonical）写出。
//      头部存 256 个码长，解码器据此重建完全相同的码表。
//      这是 gzip/DEFLATE 的核心思想（教学简化版）。
//
//   2. 自适应 Huffman（huffman_adaptive_*）
//      一遍过、不存频率表。编码器和解码器各自维护一棵"随数据增长"的树：
//      新符号先用 NYT（Not-Yet-Transmitted）节点 + 8 位原始字节发出，
//      然后双方同步增频、重建树。因为收发双方看到的字节序列完全一致，
//      两棵树始终保持一致。适合流式、无法预读整段数据的场景。
//
//   3. 规范 Huffman 工具（canonical 码长 -> 码字）
//      把"每个符号多少位"转成最短前缀码，是静态/算术编码共用的基础。
//
// 约定同 rle：encode 返回写出字节数（不足返回 -1），decode 返回还原字节数。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace compress {

// 静态 Huffman：输入整段，输出带 256 字节码长头 + 位流
int huffman_static_encode(const uint8_t* in, int n, uint8_t* out, int out_cap);
int huffman_static_decode(const uint8_t* in, int n, uint8_t* out, int out_cap);

// 自适应 Huffman：无频率头，头部仅存原始长度（u32 小端）
int huffman_adaptive_encode(const uint8_t* in, int n, uint8_t* out, int out_cap);
int huffman_adaptive_decode(const uint8_t* in, int n, uint8_t* out, int out_cap);

// 汇总自测（全零/随机/文本/二进制/单字节/空 等 round-trip + 压缩比）
int huffman_self_test();

} // namespace compress
} // namespace nefu
