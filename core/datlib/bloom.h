// nefuOS data-types library — Bloom filter (bloom)
// 布隆过滤器：用 k 个哈希函数映射到 m 位位图，用于"可能在集合中 /
// 绝不在集合中"的快速判重。允许假阳性（误报在），绝不漏报。
// 适用于缓存穿透防护、爬虫去重、拼写预检等海量判重场景。
// 本实现用双哈希构造 k 个独立哈希（h1 + i*h2 技巧），位图动态分配。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace dt {

class bloom {
public:
    // m = 位图位数；k = 哈希函数个数
    bloom(int m, int k) : bits_(0), m_(m), k_(k), inserted_(0) {
        if (m_ < 64) m_ = 64;
        if (k_ < 1) k_ = 1;
        if (k_ > 16) k_ = 16;
        bits_ = new uint64_t[(m_ + 63) / 64];
        for (int i = 0; i < (m_ + 63) / 64; i++) bits_[i] = 0;
    }
    ~bloom() { if (bits_) { delete[] bits_; bits_ = 0; } }
    bloom(const bloom&) = delete;
    bloom& operator=(const bloom&) = delete;

    // 插入字符串
    void insert(const char* s) {
        unsigned h1 = hash1(s);
        unsigned h2 = hash2(s);
        for (int i = 0; i < k_; i++) {
            unsigned h = (h1 + (unsigned)i * h2) & 0x7fffffff;
            set_bit(h % (unsigned)m_);
        }
        inserted_++;
    }
    // 查询：可能存在于集合（true）或确定不存在（false）
    bool maybe_contains(const char* s) const {
        unsigned h1 = hash1(s);
        unsigned h2 = hash2(s);
        for (int i = 0; i < k_; i++) {
            unsigned h = (h1 + (unsigned)i * h2) & 0x7fffffff;
            if (!test_bit(h % (unsigned)m_)) return false;
        }
        return true;
    }
    int inserted_count() const { return inserted_; }
    int bit_count() const { return m_; }

    // 统计置位数（评估填充率）
    int bits_set() const {
        int c = 0;
        int nw = (m_ + 63) / 64;
        for (int i = 0; i < nw; i++) {
            uint64_t w = bits_[i];
            while (w) { c += (int)(w & 1); w >>= 1; }
        }
        return c;
    }

private:
    uint64_t* bits_;
    int m_;
    int k_;
    int inserted_;

    void set_bit(unsigned b) { bits_[b >> 6] |= (uint64_t)1 << (b & 63); }
    bool test_bit(unsigned b) const { return (bits_[b >> 6] >> (b & 63)) & 1; }

    // FNV-1a 变体（两个不同种子）
    static unsigned hash1(const char* s) {
        unsigned h = 2166136261u;
        for (int i = 0; s[i]; i++) { h ^= (unsigned char)s[i]; h *= 16777619u; }
        return h;
    }
    static unsigned hash2(const char* s) {
        unsigned h = 0x811c9dc5u ^ 0x9e3779b9u;
        for (int i = 0; s[i]; i++) { h ^= (unsigned char)s[i]; h *= 16777619u; h ^= h >> 13; }
        return h;
    }
};

int bloom_self_test();

} // namespace dt
} // namespace nefu
