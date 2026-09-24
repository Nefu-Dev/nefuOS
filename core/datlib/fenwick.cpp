// nefuOS data-types library — fenwick implementation
#include "fenwick.h"
#include <stdio.h>

namespace nefu {
namespace dt {

void fenwick::build(const int* a, int n) {
    if (tree_) { delete[] tree_; tree_ = 0; }
    n_ = n;
    if (n <= 0) return;
    tree_ = new int[n + 1];
    for (int i = 1; i <= n; i++) tree_[i] = 0;
    for (int i = 0; i < n; i++) add(i, a[i]);
}

void fenwick::add(int pos, int delta) {
    // 沿 lowbit 向上爬：pos -> pos+lowbit(pos) -> ...
    for (int i = pos + 1; i <= n_; i += lowbit(i)) tree_[i] += delta;
}

int fenwick::prefix_sum(int pos) const {
    // 空树保护：未 build 或 n<=0 时返回 0
    if (!tree_ || n_ <= 0) return 0;
    // 沿 lowbit 向下分解
    int s = 0;
    for (int i = pos + 1; i > 0; i -= lowbit(i)) s += tree_[i];
    return s;
}

int fenwick::range_sum(int l, int r) const {
    if (l < 0) l = 0;
    if (r >= n_) r = n_ - 1;
    if (l > r || n_ <= 0) return 0;
    return prefix_sum(r) - (l > 0 ? prefix_sum(l - 1) : 0);
}

int fenwick::lower_bound(int k) const {
    // 倍增二分：从最高位试起，累积前缀逼近 k
    int idx = 0;
    int bit = 1;
    while ((bit << 1) <= n_) bit <<= 1;
    for (; bit; bit >>= 1) {
        int ni = idx + bit;
        if (ni <= n_ && tree_[ni] < k) {
            k -= tree_[ni];
            idx = ni;
        }
    }
    return idx;    // 返回内部下标（0..n-1 平移）；idx==n 表示超界
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int fenwick_self_test() {
    g_fails = 0;
    {
        int a[] = {3, 1, 4, 1, 5, 9, 2, 6};
        fenwick fw;
        fw.build(a, 8);
        expect("fw-prefix", fw.prefix_sum(0) == 3);
        expect("fw-prefix2", fw.prefix_sum(7) == 31);
        expect("fw-prefix3", fw.prefix_sum(4) == 14);
        expect("fw-range", fw.range_sum(2, 5) == 19);
        expect("fw-range2", fw.range_sum(0, 7) == 31);
        expect("fw-point", fw.point(3) == 1 && fw.point(7) == 6);
        // 单点更新
        fw.add(3, 10);
        expect("fw-add", fw.point(3) == 11);
        expect("fw-add-prefix", fw.prefix_sum(4) == 24);
        fw.set(0, 100);
        expect("fw-set", fw.point(0) == 100);
        // total
        expect("fw-total", fw.total() == 138);
        // 全 1 数组 + lower_bound
        int ones[16];
        for (int i = 0; i < 16; i++) ones[i] = 1;
        fenwick f2;
        f2.build(ones, 16);
        expect("fw-lb", f2.lower_bound(5) == 4);      // 前 5 个 1 的末下标 4
        expect("fw-lb2", f2.lower_bound(1) == 0);
        expect("fw-lb3", f2.lower_bound(16) == 15);
        // 空
        fenwick f3;
        f3.build(ones, 0);
        expect("fw-empty", f3.total() == 0 && f3.prefix_sum(0) == 0);
    }
    {
        // 逆序对计数（经典应用）：统计 j<i 且 a[j]>a[i]
        int a[] = {5, 3, 2, 4, 1};
        fenwick cnt;
        int m = 5;
        int zeros[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        cnt.build(zeros, m + 1);     // 值域桶 1..5
        long long inv = 0;
        for (int i = 0; i < 5; i++) {
            // 已插入中 > a[i] 的个数 = 总数 - 前缀 <= a[i]
            inv += cnt.range_sum(a[i] + 1, m);
            cnt.add(a[i], 1);
        }
        expect("fw-inv", inv == 8);   // 5,3,2,4,1 的逆序对为 8
    }
    return g_fails;
}

} // namespace dt
} // namespace nefu
