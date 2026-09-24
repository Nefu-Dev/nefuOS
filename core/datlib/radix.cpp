// nefuOS data-types library — radix implementation & helpers
#include "radix.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace dt {

// ---- 便捷工具 ----

// 拼写检查：字典中存在的词返回 0；否则返回与字典的最小编辑距离（简化
// 用前缀匹配代替：返回字典中最长公共前缀长度，0 表示完全陌生）。
int radix_spell_hint(const radix& dict, const char* word) {
    if (dict.contains(word)) return 0;
    // 找最长公共前缀：逐字符检查字典是否有该前缀
    int best = 0;
    char buf[64];
    for (int i = 0; word[i] && i < 63; i++) {
        buf[i] = word[i];
        buf[i + 1] = 0;
        int pc = dict.prefix_count(buf);
        if (pc > 0) best = i + 1;
    }
    return best;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int radix_self_test() {
    g_fails = 0;
    {
        radix t;
        t.insert("apple");
        t.insert("app");
        t.insert("application");
        t.insert("banana");
        t.insert("apple");            // 重复
        expect("rx-count", t.word_count() == 5);       // 5 次插入
        expect("rx-freq", t.count_of("apple") == 2);
        expect("rx-contains", t.contains("app") && t.contains("banana"));
        expect("rx-none", !t.contains("ap") && !t.contains("banan"));
        // 前缀统计
        expect("rx-prefix", t.prefix_count("app") == 3);    // app/apple/application
        expect("rx-prefix2", t.prefix_count("ban") == 1);
        expect("rx-prefix-none", t.prefix_count("zzz") == 0);
        // 自动补全
        char out[8][64];
        int n = t.complete("app", out, 8);
        expect("rx-complete", n == 3);
        bool ok = false;
        for (int i = 0; i < n; i++) if (strcmp(out[i], "app") == 0) ok = true;
        expect("rx-complete-app", ok);
        ok = false;
        for (int i = 0; i < n; i++) if (strcmp(out[i], "application") == 0) ok = true;
        expect("rx-complete-app2", ok);
        // 删除
        expect("rx-remove", t.remove("apple"));
        expect("rx-remove2", t.remove("apple"));
        expect("rx-remove-none", !t.remove("apple"));
        expect("rx-after-del", t.count_of("apple") == 0 && t.contains("app"));
        expect("rx-after-del-prefix", t.prefix_count("app") == 2);
        expect("rx-wordcount", t.word_count() == 3);
        // 拼写提示
        expect("rx-hint", radix_spell_hint(t, "app") == 0);        // 存在
        expect("rx-hint2", radix_spell_hint(t, "apz") == 2);       // "ap" 前缀在
        expect("rx-hint3", radix_spell_hint(t, "xyz") == 0);
    }
    {
        // 大数据量
        radix t;
        char w[8];
        for (int i = 0; i < 1000; i++) {
            int v = (i * 31) % 676;         // 两字母组合 aa..zz
            w[0] = (char)('a' + v / 26);
            w[1] = (char)('a' + v % 26);
            w[2] = 0;
            t.insert(w);
        }
        bool ok = true;
        for (int i = 0; i < 1000; i++) {
            int v = (i * 31) % 676;
            w[0] = (char)('a' + v / 26);
            w[1] = (char)('a' + v % 26);
            w[2] = 0;
            if (!t.contains(w)) ok = false;
        }
        expect("rx-mass", ok);
        expect("rx-mass-prefix", t.prefix_count("a") == 26);
    }
    return g_fails;
}

} // namespace dt
} // namespace nefu
