// nefuOS data-types library — sortedlist implementation & helpers
#include "sortedlist.h"
#include <stdio.h>

namespace nefu {
namespace dt {

// ---- 便捷工具 ----

// 找第 k 大的数（经典"数据流中位数/第 k 大"场景，与 kth 配合）
int sortedlist_kth_largest(sortedlist& sl, int k) {
    // 第 k 大 = 第 (total - k) 小
    int t = sl.total();
    if (k < 1) k = 1;
    if (k > t) k = t;
    int v = 0;
    sl.kth(t - k, v);
    return v;
}

// 求数组中出现次数超过一半的众数（若存在）——用有序列表 + 中位验证
bool sortedlist_majority(const int* a, int n, int& maj) {
    sortedlist sl;
    for (int i = 0; i < n; i++) sl.insert(a[i]);
    int m;
    if (!sl.median(m)) return false;
    // 验证中位数出现次数是否 > n/2
    int c = 0;
    for (int i = 0; i < n; i++) if (a[i] == m) c++;
    if (c > n / 2) { maj = m; return true; }
    return false;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int sortedlist_self_test() {
    g_fails = 0;
    {
        sortedlist sl;
        expect("sl-empty", sl.empty());
        int a[] = {5, 1, 9, 3, 7, 3, 5, 5};
        for (int i = 0; i < 8; i++) sl.insert(a[i]);
        expect("sl-size", sl.size() == 5);        // 去重 5 个不同值
        expect("sl-total", sl.total() == 8);
        // 有序转储
        int out[16];
        int n = sl.dump(out, 16);
        expect("sl-order", n == 5 && out[0] == 1 && out[1] == 3 && out[2] == 5 &&
                           out[3] == 7 && out[4] == 9);
        expect("sl-contains", sl.contains(3) && sl.contains(9) && !sl.contains(4));
        // 第 k 小（含重复展开）：1,3,3,5,5,5,7,9
        int v = 0;
        expect("sl-kth0", sl.kth(0, v) && v == 1);
        expect("sl-kth2", sl.kth(2, v) && v == 3);
        expect("sl-kth5", sl.kth(5, v) && v == 5);
        expect("sl-kth-none", !sl.kth(8, v));
        // 小于计数
        expect("sl-less", sl.count_less(5) == 3);
        expect("sl-less2", sl.count_less(1) == 0);
        expect("sl-less3", sl.count_less(100) == 8);
        // 中位数（8 个 → 第 4 个 = 5）
        expect("sl-median", sl.median(v) && v == 5);
        // 删除：3 只有 2 个 → 删两次后第三次失败
        expect("sl-remove", sl.remove(3));
        expect("sl-remove2", sl.remove(3));
        expect("sl-remove-none", !sl.remove(3));
        expect("sl-total2", sl.total() == 6);
        expect("sl-size2", sl.size() == 4);
        expect("sl-kth-new", sl.kth(2, v) && v == 5);   // 1,5,5,5,7,9
        // 第 k 大
        expect("sl-kthlargest", sortedlist_kth_largest(sl, 1) == 9);
        expect("sl-kthlargest2", sortedlist_kth_largest(sl, 3) == 5);
        // 大序列排序验证
        sortedlist big;
        for (int i = 99; i >= 0; i--) big.insert(i % 7);
        int bigout[32];
        int bn = big.dump(bigout, 32);
        expect("sl-big", bn == 7 && bigout[0] == 0 && bigout[6] == 6);
        expect("sl-big-total", big.total() == 100);
    }
    {
        // 众数
        int a[] = {1, 2, 3, 2, 2, 2, 5};
        int maj = 0;
        expect("sl-majority", sortedlist_majority(a, 7, maj) && maj == 2);
        int b[] = {1, 2, 3, 4};
        expect("sl-majority-none", !sortedlist_majority(b, 4, maj));
    }
    return g_fails;
}

} // namespace dt
} // namespace nefu
