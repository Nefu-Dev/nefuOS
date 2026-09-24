// nefuOS 压缩算法库 —— 内部公共头（不对外暴露）
// 本头文件只给 core/compress/ 内部各模块使用：
//   1) 提供 bfill/bcopy/bcmp 三个不可被编译器误优化的内存助手。
//      （本机 mingw g++ -O2 会把 memset/memcpy 的内建展开错误地编成死循环/栈溢出，
//        见 klib/memory.cpp 的 strncpy 注释。这里一律用 volatile 访问，
//        与 memory.cpp 的规避手法保持一致，且不依赖编译命令加 -fno-builtin。）
//   2) 提供 BitSink / BitSource 两个极简位流读写器，
//      供 Huffman / 算术编码 / 变长整数等按比特读写的算法复用。
//
// 设计约束（全库统一遵守）：
//   - 命名空间 nefu::compress
//   - 禁止 STL 容器，一律 new[]/delete[]
//   - 禁止异常、RTTI（编译期 -fno-exceptions -fno-rtti）
//   - 禁止 malloc（库内部只走 new[]/delete[]，最终映射到 kalloc/kfree）
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace compress {

// ---------------------------------------------------------------------
// 内存助手（volatile 版，规避 mingw -O2 内建误优化）
// ---------------------------------------------------------------------
// 把 dst 的前 n 字节填成 c
inline void bfill(void* dst, uint8_t c, size_t n) {
    volatile uint8_t* d = (volatile uint8_t*)dst;
    while (n--) *d++ = c;
}
// 复制 n 字节
inline void bcopy(void* dst, const void* src, size_t n) {
    volatile uint8_t* d = (volatile uint8_t*)dst;
    const volatile uint8_t* s = (const volatile uint8_t*)src;
    while (n--) *d++ = *s++;
}
// 比较 n 字节，返回 0 相等
inline int bcmp(const void* a, const void* b, size_t n) {
    const volatile uint8_t* x = (const volatile uint8_t*)a;
    const volatile uint8_t* y = (const volatile uint8_t*)b;
    while (n--) { if (*x != *y) return 1; x++; y++; }
    return 0;
}
// 求字符串长度
inline int blen(const char* s) {
    int n = 0; while (s[n]) n++; return n;
}

// =====================================================================
// BitSink：按比特把数据写进一个字节缓冲区（LSB 先写，即先写进 bit0）。
// =====================================================================
class BitSink {
public:
    BitSink(uint8_t* buf, int cap)
        : buf_(buf), cap_(cap), pos_(0), bit_(0), bits_(0) {}

    void put_bit(int b) {
        bits_ |= ((uint32_t)(b & 1) << bit_);
        bit_++;
        if (bit_ == 8) flush();
    }
    void write_bits(uint32_t v, int n) {
        for (int i = 0; i < n; i++) put_bit((int)((v >> i) & 1));
    }
    void put_byte(uint8_t b) { write_bits(b, 8); }
    int finish() {
        if (bit_ > 0) flush();
        return pos_;
    }
    int byte_count() const { return pos_ + (bit_ > 0 ? 1 : 0); }
    int capacity_left() const { return cap_ - pos_ - 1; }

private:
    void flush() {
        if (pos_ < cap_) buf_[pos_] = (uint8_t)bits_;
        pos_++;
        bits_ = 0;
        bit_ = 0;
    }
    uint8_t* buf_;
    int   cap_;
    int   pos_;
    int   bit_;
    uint32_t bits_;
};

// =====================================================================
// BitSource：按比特读 BitSink 写出来的流（LSB 先读）。
// =====================================================================
class BitSource {
public:
    BitSource(const uint8_t* buf, int len)
        : buf_(buf), len_(len), pos_(0), bit_(0) {}

    int read_bit() {
        if (pos_ >= len_) return 0;
        int b = (buf_[pos_] >> bit_) & 1;
        bit_++;
        if (bit_ == 8) { pos_++; bit_ = 0; }
        return b;
    }
    uint32_t read_bits(int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; i++) v |= ((uint32_t)read_bit() << i);
        return v;
    }
    uint8_t read_byte() { return (uint8_t)read_bits(8); }
    bool eof() const { return pos_ >= len_; }
    int  tell() const { return pos_; }

private:
    const uint8_t* buf_;
    int len_;
    int pos_;
    int bit_;
};

// ---- 小工具：把 n 个字节写成小端 / 大端到缓冲区 ----
inline void put_u16_le(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF);
}
inline uint16_t get_u16_le(const uint8_t* p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
inline void put_u32_le(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}
inline uint32_t get_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

} // namespace compress
} // namespace nefu
