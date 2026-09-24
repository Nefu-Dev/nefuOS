// nefuOS compression library — bitio implementation
#include "bitio.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace comp {

BitWriter::BitWriter(unsigned char* buf, int cap) : buf_(buf), cap_(cap), pos_(0), bit_(0) {
    memset(buf_, 0, cap_);
}

void BitWriter::write_bit(int b) {
    if (pos_ >= cap_) return;
    if (b & 1) buf_[pos_] |= (unsigned char)(1u << bit_);
    bit_++;
    if (bit_ == 8) { bit_ = 0; pos_++; }
}

void BitWriter::write_bits(uint32_t value, int nbits) {
    for (int i = 0; i < nbits; i++) write_bit((value >> i) & 1);
}

int BitWriter::finish() {
    if (bit_ > 0) { pos_++; bit_ = 0; }   // 补零的字节已随写入清零
    return pos_;
}

BitReader::BitReader(const unsigned char* buf, int nbytes)
    : buf_(buf), nbytes_(nbytes), pos_(0), bit_(0), cur_(0), cur_valid_(0) {}

int BitReader::read_bit() {
    if (cur_valid_ == 0) {
        if (pos_ >= nbytes_) return 0;
        cur_ = buf_[pos_];
        cur_valid_ = 8;
        pos_++;
    }
    int b = (cur_ >> bit_) & 1;
    bit_++;
    cur_valid_--;
    if (cur_valid_ == 0) { bit_ = 0; }
    return b;
}

uint32_t BitReader::read_bits(int nbits) {
    uint32_t v = 0;
    for (int i = 0; i < nbits; i++) v |= ((uint32_t)read_bit()) << i;
    return v;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int bitio_self_test() {
    g_fails = 0;
    unsigned char buf[64];
    // 写入 12 位 0xABC（LSB 优先），回读一致
    {
        BitWriter w(buf, 64);
        w.write_bits(0xABC, 12);
        int n = w.finish();
        BitReader r(buf, n);
        expect("bitio-roundtrip", r.read_bits(12) == 0xABC);
    }
    // 混合写：1 位 + 5 位 + 10 位
    {
        BitWriter w(buf, 64);
        w.write_bit(1);
        w.write_bits(0x1F, 5);
        w.write_bits(0x3FF, 10);
        int n = w.finish();
        BitReader r(buf, n);
        expect("bitio-mix1", r.read_bit() == 1);
        expect("bitio-mix5", r.read_bits(5) == 0x1F);
        expect("bitio-mix10", r.read_bits(10) == 0x3FF);
    }
    // 恰好 8 位 = 1 字节
    {
        BitWriter w(buf, 64);
        w.write_bits(0x5A, 8);
        int n = w.finish();
        expect("bitio-byte", n == 1 && buf[0] == 0x5A);
    }
    // 跨字节：13 位
    {
        BitWriter w(buf, 64);
        w.write_bits(0x1D5, 13);
        int n = w.finish();
        expect("bitio-cross", n == 2);
        BitReader r(buf, n);
        expect("bitio-cross-read", r.read_bits(13) == 0x1D5);
    }
    return g_fails;
}

} // namespace comp
} // namespace nefu
