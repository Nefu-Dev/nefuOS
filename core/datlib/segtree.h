// nefuOS data-types library — segment tree (segtree)
// 线段树：把区间分解为 O(log n) 个节点，支持区间查询（和/最值）
// 与单点更新。经典"分治数据结构"，用于 RMQ、区间和、区间最值等。
// 本实现用数组存储（4*n 空间），支持：
//   - 单点赋值/增量更新
//   - 区间求和、区间最大值、区间最小值
//   - 区间覆盖（区间加延迟标记版见 lazy 注释——本版保持简单）
// 教学注释给出每个函数的递归语义。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace dt {

const int SEG_INF = 0x3f3f3f3f;

class segtree {
public:
    segtree() : n_(0), sum_(0), mx_(0), mn_(0) {}
    // 用数组 a[0..n-1] 建树
    void build(const int* a, int n);
    ~segtree() { free_all(); }
    segtree(const segtree&) = delete;
    segtree& operator=(const segtree&) = delete;

    // 单点更新：a[pos] = val（覆盖式）
    void set(int pos, int val);
    // 单点增量：a[pos] += delta
    void add(int pos, int delta);
    // 区间 [l, r]（闭区间）求和
    int query_sum(int l, int r) const;
    int query_max(int l, int r) const;
    int query_min(int l, int r) const;
    int size() const { return n_; }

private:
    int n_;
    int* sum_;
    int* mx_;
    int* mn_;

    void free_all() {
        if (sum_) { delete[] sum_; sum_ = 0; }
        if (mx_) { delete[] mx_; mx_ = 0; }
        if (mn_) { delete[] mn_; mn_ = 0; }
    }
    void build_node(int o, int l, int r, const int* a);
    void set_node(int o, int l, int r, int pos, int val);
    void add_node(int o, int l, int r, int pos, int delta);
    int sum_node(int o, int l, int r, int ql, int qr) const;
    int max_node(int o, int l, int r, int ql, int qr) const;
    int min_node(int o, int l, int r, int ql, int qr) const;
};

int segtree_self_test();

} // namespace dt
} // namespace nefu
