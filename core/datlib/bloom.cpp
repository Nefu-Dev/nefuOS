// nefuOS data-types library — bloom implementation & helpers
#include "bloom.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace dt {

// ---- 便捷工具 ----

// 简易拼写预检器：把字典插入布隆过滤器，然后检查单词。
// 返回"确定不在字典"的单词数（假阳性会使计数偏低）。
struct BloomDict {
    bloom b;
    BloomDict() : b(4096, 4) {}
    void add_word(const char* w) { b.insert(w); }
    bool maybe_word(const char* w) const { return b.maybe_contains(w); }
};

// 估算假阳性率：向过滤器插入 n 个词后，检查未插入词被误报的比例。
// probe 为检查的未插入词数量。
double bloom_false_positive_rate(const bloom& b, const char** miss_words, int nprobe) {
    int hit = 0;
    for (int i = 0; i < nprobe; i++) if (b.maybe_contains(miss_words[i])) hit++;
    return nprobe ? (double)hit / (double)nprobe : 0.0;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int bloom_self_test() {
    g_fails = 0;
    {
        bloom b(1024, 4);
        const char* words[] = {"apple", "banana", "cherry", "date", "elderberry"};
        for (int i = 0; i < 5; i++) b.insert(words[i]);
        expect("bl-count", b.inserted_count() == 5);
        // 所有已插入词必命中（无漏报）
        bool ok = true;
        for (int i = 0; i < 5; i++) if (!b.maybe_contains(words[i])) ok = false;
        expect("bl-nofalse-neg", ok);
        expect("bl-bits", b.bits_set() > 0 && b.bits_set() <= 1024);
        // 未插入词基本不命中（允许少量假阳性，此处取保守断言 <= 2 个）
        const char* misses[] = {"fig", "grape", "honeydew", "kiwi", "lemon"};
        int fp = 0;
        for (int i = 0; i < 5; i++) if (b.maybe_contains(misses[i])) fp++;
        expect("bl-low-fp", fp <= 2);
        // 更大的过滤器假阳性率更低
        bloom big(1 << 16, 4);
        const char* dict[] = {"hello", "world", "nefu", "os", "cpp", "kernel", "driver", "gui"};
        for (int i = 0; i < 8; i++) big.insert(dict[i]);
        const char* miss2[] = {"xyzzy", "qwerty", "asdfgh", "zxcvbn", "poiuyt", "lkjhg", "mnbvc", "abcdef",
                               "ghijkl", "rstuvw", "123456", "foobar"};
        double rate = bloom_false_positive_rate(big, miss2, 12);
        expect("bl-fp-rate", rate < 0.2);       // 16k 位 8 词 4 哈希，理论 < 2%
    }
    {
        // 拼写预检场景
        BloomDict d;
        d.add_word("kernel");
        d.add_word("driver");
        d.add_word("system");
        expect("bl-dict-in", d.maybe_word("kernel") && d.maybe_word("driver"));
        expect("bl-dict-out", !d.maybe_word("nonsense"));
        // 空过滤器
        bloom e(64, 2);
        expect("bl-empty", !e.maybe_contains("anything"));
        expect("bl-empty-count", e.bits_set() == 0);
    }
    return g_fails;
}

} // namespace dt
} // namespace nefu
