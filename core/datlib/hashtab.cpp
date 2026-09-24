// nefuOS data-types library — hashtab self test & helpers
#include "hashtab.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace dt {

// ---- 便捷工具 ----

// 词频统计：把 text 按空白切词，统计每个词出现次数。
// 返回不同词数；words[i]/counts[i] 输出（容量 cap）。
int hashtab_word_count(const char* text, char words[][64], int counts[], int cap) {
    hashtab<int> h;
    int total = 0;
    const char* p = text;
    char buf[64];
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        int n = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && n < 63)
            buf[n++] = *p++;
        buf[n] = 0;
        // 简单字符串哈希（教学版：直接叠加）
        int hk = 0;
        for (int i = 0; i < n; i++) hk = hk * 31 + (unsigned char)buf[i];
        int v;
        if (h.get(hk, v)) { h.put(hk, v + 1); }
        else { h.put(hk, 1); total++; }
    }
    // 导出（按哈希表存储顺序）
    int k = 0;
    for (int i = h.first_live(); i >= 0 && k < cap; i = h.next_live(i)) {
        // 反向哈希不可行，这里只导出数量
        counts[k] = h.val_at(i);
        words[k][0] = 0;
        k++;
    }
    return total;
}

// 两数之和：找 a 中两下标 i<j 使 a[i]+a[j]==target；写入 out 返回 1
int hashtab_two_sum(const int* a, int n, int target, int* out) {
    hashtab<int> h;
    for (int i = 0; i < n; i++) {
        int need = target - a[i];
        int j;
        if (h.get(need, j)) { out[0] = j; out[1] = i; return 1; }
        h.put(a[i], i);
    }
    return 0;
}

// 数组去重（保持首次出现顺序）；返回去重后长度
int hashtab_dedup(const int* a, int n, int* out) {
    hashtab<int> seen;
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (!seen.contains(a[i])) { seen.put(a[i], 1); out[k++] = a[i]; }
    }
    return k;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int hashtab_self_test() {
    g_fails = 0;
    {
        hashtab<int> h;
        expect("ht-empty", h.empty());
        expect("ht-put", h.put(10, 100));
        expect("ht-put2", h.put(20, 200));
        expect("ht-update", !h.put(10, 999));       // 覆盖
        int v = 0;
        expect("ht-get", h.get(10, v) && v == 999);
        expect("ht-get2", h.get(20, v) && v == 200);
        expect("ht-get-none", !h.get(30, v));
        expect("ht-contains", h.contains(20) && !h.contains(25));
        // 删除（墓碑）
        expect("ht-remove", h.remove(20));
        expect("ht-remove2", !h.remove(20));
        expect("ht-remove-get", !h.get(20, v));
        // 删除后同槽新键可插入
        expect("ht-reinsert", h.put(20, 888));
        expect("ht-reinsert-get", h.get(20, v) && v == 888);
        expect("ht-size", h.size() == 2);
        // 迭代
        int seen[2] = {0, 0};
        for (int i = h.first_live(); i >= 0; i = h.next_live(i)) {
            if (h.key_at(i) == 10) seen[0] = 1;
            if (h.key_at(i) == 20) seen[1] = 1;
        }
        expect("ht-iter", seen[0] && seen[1]);
        // 大规模 + 扩容
        hashtab<int> big(8);
        bool ok = true;
        for (int i = 0; i < 1000; i++) big.put(i * 7, i);
        for (int i = 0; i < 1000; i++) {
            int w;
            if (!big.get(i * 7, w) || w != i) ok = false;
        }
        expect("ht-grow", ok && big.size() == 1000 && big.capacity() >= 1024);
        expect("ht-grow-cap", big.capacity() <= 2048);
        big.clear();
        expect("ht-clear", big.empty());
    }
    {
        // 两数之和
        int a[] = {2, 7, 11, 15};
        int out[2];
        expect("ht-twosum", hashtab_two_sum(a, 4, 9, out) == 1 && out[0] == 0 && out[1] == 1);
        expect("ht-twosum-none", hashtab_two_sum(a, 4, 100, out) == 0);
        // 去重
        int b[] = {3, 1, 3, 2, 1, 4};
        int dd[8];
        int dn = hashtab_dedup(b, 6, dd);
        expect("ht-dedup", dn == 4 && dd[0] == 3 && dd[1] == 1 && dd[2] == 2 && dd[3] == 4);
    }
    {
        // 泛型值：字符串指针
        hashtab<const char*> h;
        h.put(1, "one");
        h.put(2, "two");
        const char* s = 0;
        expect("ht-strval", h.get(1, s) && strcmp(s, "one") == 0);
        expect("ht-strval2", h.get(2, s) && strcmp(s, "two") == 0);
    }
    return g_fails;
}

} // namespace dt
} // namespace nefu
