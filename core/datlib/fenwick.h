// nefuOS data-types library — Fenwick tree (fenwick, binary indexed tree)
// 树状数组：用"最低位 1"（lowbit）把前缀和分解成 O(log n) 段，
// 单点更新与前缀查询都是 O(log n)。比线段树更省内存（n+1 个单元）。
// 经典应用：动态前缀和、逆序对计数、区间加区间查（差分）。
// 下标从 1 开始（0 号单元闲置，便于 lowbit 运算）。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace dt {

class fenwick {
public:
    fenwick() : n_(0), tree_(0) {}
    // 建树（下标 0..n-1，内部平移 1）
    void build(const int* a, int n);
    ~fenwick() { if (tree_) { delete[] tree_; tree_ = 0; } }
    fenwick(const fenwick&) = delete;
    fenwick& operator=(const fenwick&) = delete;

    // a[pos] += delta（pos 从 0 起）
    void add(int pos, int delta);
    // a[pos] = val（先算差值再 add）
    void set(int pos, int val) { int cur = point(pos); add(pos, val - cur); }
    // 前缀和 a[0..pos]
    int prefix_sum(int pos) const;
    // 区间和 a[l..r]
    int range_sum(int l, int r) const;
    // 单点值
    int point(int pos) const { return prefix_sum(pos) - (pos > 0 ? prefix_sum(pos - 1) : 0); }
    // 全局和
    int total() const { return n_ ? prefix_sum(n_ - 1) : 0; }
    int size() const { return n_; }
    // 前缀和 >= k 的最小下标（二分，需 tree 单调——非负增量场景）
    int lower_bound(int k) const;

private:
    int n_;
    int* tree_;
    static int lowbit(int x) { return x & (-x); }
};

int fenwick_self_test();

} // namespace dt
} // namespace nefu
