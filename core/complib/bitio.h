// nefuOS compression library — 位级读写器 (bitio)
// 位流：压缩算法（Huffman/算术编码）需要逐位读写，而不是逐字节。
// 本模块实现"LSB 优先"（先写低比特位）的位写入器/读取器，
// 是后续所有压缩模块的公共基座。教学注释含比特序说明。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace comp {

// 位写入器：把位逐个写入字节缓冲（LSB 优先）
class BitWriter {
public:
    BitWriter(unsigned char* buf, int cap);
    // 写 1 位（b 为 0 或 1）
    void write_bit(int b);
    // 写低 nbits 位（nbits 1..32，value 低位在前）
    void write_bits(uint32_t value, int nbits);
    // 末尾不足字节补 0；返回总字节数
    int finish();
    int byte_pos() const { return pos_; }
private:
    unsigned char* buf_;
    int cap_;
    int pos_;        // 当前字节位置
    int bit_;        // 当前字节已写位数 0..7
};

// 位读取器
class BitReader {
public:
    BitReader(const unsigned char* buf, int nbytes);
    int read_bit();
    // 读 nbits 位（低位在前）
    uint32_t read_bits(int nbits);
    int byte_pos() const { return pos_; }
    bool eof() const { return pos_ >= nbytes_ && bit_ == 0; }
private:
    const unsigned char* buf_;
    int nbytes_;
    int pos_;
    int bit_;
    int cur_;        // 当前字节
    int cur_valid_;  // 当前字节有效位数
};

int bitio_self_test();

} // namespace comp
} // namespace nefu
