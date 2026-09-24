// nefuOS data-types library — dynamic bitset
// 动态位集：以 64 位字为基本单元存储比特位，支持置位/清零/翻转/
// 查询/位运算（与/或/异或/取反）/区间统计。适用于布隆过滤器、状态
// 压缩、位图索引等。内存 = ceil(n/64)*8 字节。
// 全部实现从零手写（无 STL），教学注释解释每个位运算。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace dt {

class bitset {
public:
    explicit bitset(int nbits) : nbits_(nbits), nwords_((nbits + 63) / 64), words_(0) {
        words_ = new uint64_t[nwords_ ? nwords_ : 1];
        reset_all();
    }
    ~bitset() { if (words_) { delete[] words_; words_ = 0; } }
    bitset(const bitset&) = delete;
    bitset& operator=(const bitset&) = delete;

    int size() const { return nbits_; }

    // 置位/清零/翻转/查询 --------------------------------------
    void set(int i) { words_[i >> 6] |= (uint64_t)1 << (i & 63); }
    void clear(int i) { words_[i >> 6] &= ~((uint64_t)1 << (i & 63)); }
    void flip(int i) { words_[i >> 6] ^= (uint64_t)1 << (i & 63); }
    bool test(int i) const { return (words_[i >> 6] >> (i & 63)) & 1; }

    void set_all() { for (int i = 0; i < nwords_; i++) words_[i] = ~(uint64_t)0; mask_tail(); }
    void reset_all() { for (int i = 0; i < nwords_; i++) words_[i] = 0; }
    void flip_all() { for (int i = 0; i < nwords_; i++) words_[i] = ~words_[i]; mask_tail(); }

    // 位运算（长度不同则按较短者；结果写入 this）----------------
    void bitwise_and(const bitset& o) { op_and(o, false); }
    void bitwise_or(const bitset& o)  { op_and(o, true); }
    void bitwise_xor(const bitset& o) { op_xor(o); }

    // 统计 ----------------------------------------------------
    // 置位个数（popcount，逐字累加）
    int count() const {
        int c = 0;
        for (int i = 0; i < nwords_; i++) c += popcount64(words_[i]);
        return c;
    }
    bool any() const {
        for (int i = 0; i < nwords_; i++) if (words_[i]) return true;
        return false;
    }
    bool none() const { return !any(); }
    bool all() const {
        int rem = nbits_ & 63;
        for (int i = 0; i < nwords_; i++) {
            uint64_t want = ~(uint64_t)0;
            if (i == nwords_ - 1 && rem) want = (uint64_t)(((uint64_t)1 << rem) - 1);
            if (words_[i] != want) return false;
        }
        return true;
    }
    // 第一个置位下标；无则 -1
    int first_set() const {
        for (int i = 0; i < nwords_; i++) {
            if (words_[i]) {
                uint64_t w = words_[i];
                int bit = 0;
                while (!(w & 1)) { w >>= 1; bit++; }
                return i * 64 + bit;
            }
        }
        return -1;
    }
    // 区间 [l, r] 内置位个数
    int range_count(int l, int r) const {
        if (l < 0) l = 0;
        if (r >= nbits_) r = nbits_ - 1;
        if (l > r) return 0;
        int c = 0;
        for (int i = l; i <= r; i++) if (test(i)) c++;
        return c;
    }

    // 导出到字节数组（大端，每字节 8 位）
    void to_bytes(uint8_t* out, int nbytes) const {
        for (int i = 0; i < nbytes; i++) {
            uint8_t b = 0;
            for (int k = 0; k < 8; k++) {
                int idx = i * 8 + k;
                if (idx < nbits_ && test(idx)) b |= (uint8_t)(1 << k);
            }
            out[i] = b;
        }
    }

private:
    // 屏蔽尾部多余位（nbits 不是 64 倍数时）
    void mask_tail() {
        int rem = nbits_ & 63;
        if (rem && nwords_) {
            uint64_t keep = (uint64_t)(((uint64_t)1 << rem) - 1);
            words_[nwords_ - 1] &= keep;
        }
    }
    void op_and(const bitset& o, bool is_or) {
        int n = nwords_ < o.nwords_ ? nwords_ : o.nwords_;
        for (int i = 0; i < n; i++) {
            words_[i] = is_or ? (words_[i] | o.words_[i]) : (words_[i] & o.words_[i]);
        }
        mask_tail();
    }
    void op_xor(const bitset& o) {
        int n = nwords_ < o.nwords_ ? nwords_ : o.nwords_;
        for (int i = 0; i < n; i++) words_[i] ^= o.words_[i];
        mask_tail();
    }
    static int popcount64(uint64_t w) {
        int c = 0;
        while (w) { c += (int)(w & 1); w >>= 1; }
        return c;
    }

    int nbits_;
    int nwords_;
    uint64_t* words_;
};

int bitset_self_test();

} // namespace dt
} // namespace nefu
