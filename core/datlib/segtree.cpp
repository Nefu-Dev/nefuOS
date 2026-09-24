// nefuOS data-types library — segtree implementation
#include "segtree.h"
#include <stdio.h>

namespace nefu {
namespace dt {

void segtree::build(const int* a, int n) {
    free_all();
    n_ = n;
    if (n <= 0) return;
    int cap = n * 4 + 4;
    sum_ = new int[cap];
    mx_ = new int[cap];
    mn_ = new int[cap];
    build_node(1, 0, n_ - 1, a);
}

void segtree::build_node(int o, int l, int r, const int* a) {
    if (l == r) { sum_[o] = mx_[o] = mn_[o] = a[l]; return; }
    int m = l + (r - l) / 2;
    build_node(o * 2, l, m, a);
    build_node(o * 2 + 1, m + 1, r, a);
    sum_[o] = sum_[o * 2] + sum_[o * 2 + 1];
    mx_[o] = mx_[o * 2] > mx_[o * 2 + 1] ? mx_[o * 2] : mx_[o * 2 + 1];
    mn_[o] = mn_[o * 2] < mn_[o * 2 + 1] ? mn_[o * 2] : mn_[o * 2 + 1];
}

void segtree::set_node(int o, int l, int r, int pos, int val) {
    if (l == r) { sum_[o] = mx_[o] = mn_[o] = val; return; }
    int m = l + (r - l) / 2;
    if (pos <= m) set_node(o * 2, l, m, pos, val);
    else set_node(o * 2 + 1, m + 1, r, pos, val);
    sum_[o] = sum_[o * 2] + sum_[o * 2 + 1];
    mx_[o] = mx_[o * 2] > mx_[o * 2 + 1] ? mx_[o * 2] : mx_[o * 2 + 1];
    mn_[o] = mn_[o * 2] < mn_[o * 2 + 1] ? mn_[o * 2] : mn_[o * 2 + 1];
}

void segtree::add_node(int o, int l, int r, int pos, int delta) {
    if (l == r) {
        sum_[o] += delta; mx_[o] += delta; mn_[o] += delta;
        return;
    }
    int m = l + (r - l) / 2;
    if (pos <= m) add_node(o * 2, l, m, pos, delta);
    else add_node(o * 2 + 1, m + 1, r, pos, delta);
    sum_[o] = sum_[o * 2] + sum_[o * 2 + 1];
    mx_[o] = mx_[o * 2] > mx_[o * 2 + 1] ? mx_[o * 2] : mx_[o * 2 + 1];
    mn_[o] = mn_[o * 2] < mn_[o * 2 + 1] ? mn_[o * 2] : mn_[o * 2 + 1];
}

void segtree::set(int pos, int val) {
    if (pos < 0 || pos >= n_) return;
    set_node(1, 0, n_ - 1, pos, val);
}

void segtree::add(int pos, int delta) {
    if (pos < 0 || pos >= n_) return;
    add_node(1, 0, n_ - 1, pos, delta);
}

// 区间查询：三种标准"分治剪枝"写法（sum 做示例，max/min 同构）
int segtree::sum_node(int o, int l, int r, int ql, int qr) const {
    if (ql <= l && r <= qr) return sum_[o];      // 全包含
    int m = l + (r - l) / 2;
    int res = 0;
    if (ql <= m) res += sum_node(o * 2, l, m, ql, qr);
    if (qr > m)  res += sum_node(o * 2 + 1, m + 1, r, ql, qr);
    return res;
}

int segtree::max_node(int o, int l, int r, int ql, int qr) const {
    if (ql <= l && r <= qr) return mx_[o];
    int m = l + (r - l) / 2;
    int res = -SEG_INF;
    if (ql <= m) { int t = max_node(o * 2, l, m, ql, qr); if (t > res) res = t; }
    if (qr > m)  { int t = max_node(o * 2 + 1, m + 1, r, ql, qr); if (t > res) res = t; }
    return res;
}

int segtree::min_node(int o, int l, int r, int ql, int qr) const {
    if (ql <= l && r <= qr) return mn_[o];
    int m = l + (r - l) / 2;
    int res = SEG_INF;
    if (ql <= m) { int t = min_node(o * 2, l, m, ql, qr); if (t < res) res = t; }
    if (qr > m)  { int t = min_node(o * 2 + 1, m + 1, r, ql, qr); if (t < res) res = t; }
    return res;
}

int segtree::query_sum(int l, int r) const {
    if (l < 0) l = 0;
    if (r >= n_) r = n_ - 1;
    if (l > r || n_ <= 0) return 0;
    return sum_node(1, 0, n_ - 1, l, r);
}

int segtree::query_max(int l, int r) const {
    if (l < 0) l = 0;
    if (r >= n_) r = n_ - 1;
    if (l > r || n_ <= 0) return -SEG_INF;
    return max_node(1, 0, n_ - 1, l, r);
}

int segtree::query_min(int l, int r) const {
    if (l < 0) l = 0;
    if (r >= n_) r = n_ - 1;
    if (l > r || n_ <= 0) return SEG_INF;
    return min_node(1, 0, n_ - 1, l, r);
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int segtree_self_test() {
    g_fails = 0;
    {
        int a[] = {1, 3, 5, 7, 9, 11};
        segtree st;
        st.build(a, 6);
        expect("st-sum-all", st.query_sum(0, 5) == 36);
        expect("st-sum-mid", st.query_sum(1, 3) == 15);
        expect("st-max", st.query_max(0, 5) == 11);
        expect("st-max2", st.query_max(2, 4) == 9);
        expect("st-min", st.query_min(0, 5) == 1);
        expect("st-min2", st.query_min(1, 2) == 3);
        // 单点覆盖
        st.set(0, 100);
        expect("st-set", st.query_sum(0, 0) == 100);
        expect("st-set-sum", st.query_sum(0, 5) == 135);
        expect("st-set-max", st.query_max(0, 5) == 100);
        // 单点增量
        st.add(5, 10);
        expect("st-add", st.query_sum(0, 5) == 145);
        expect("st-add-max", st.query_max(0, 5) == 100);
        // 奇偶区间
        expect("st-even", st.query_sum(0, 0) == 100 && st.query_max(4, 4) == 9);
        expect("st-clamp", st.query_sum(-5, 100) == 145);   // 越界裁剪
        // 重建小数组
        int b[] = {5};
        segtree st2;
        st2.build(b, 1);
        expect("st-single", st2.query_sum(0, 0) == 5 && st2.query_max(0, 0) == 5);
        // 空数组
        segtree st3;
        st3.build(b, 0);
        expect("st-empty", st3.query_sum(0, 0) == 0);
    }
    {
        // 随机对拍：暴力验证 200 次操作
        int a[32];
        int n = 32;
        for (int i = 0; i < n; i++) a[i] = (i * 13) % 100 - 30;
        segtree st;
        st.build(a, n);
        bool ok = true;
        // 检查全区间
        for (int l = 0; l < n; l++) {
            for (int r = l; r < n; r++) {
                int s = 0, mx = -SEG_INF, mn = SEG_INF;
                for (int k = l; k <= r; k++) { s += a[k]; if (a[k] > mx) mx = a[k]; if (a[k] < mn) mn = a[k]; }
                if (st.query_sum(l, r) != s) ok = false;
                if (st.query_max(l, r) != mx) ok = false;
                if (st.query_min(l, r) != mn) ok = false;
            }
        }
        expect("st-bruteforce", ok);
        // 随机更新后抽查
        for (int t = 0; t < 50; t++) {
            int pos = (t * 7) % n;
            int delta = (t % 5) - 2;
            a[pos] += delta;
            st.add(pos, delta);
        }
        ok = true;
        for (int i = 0; i < n; i++) if (st.query_sum(i, i) != a[i]) ok = false;
        expect("st-updates", ok);
        // a[i] = (i*13)%100 - 30 的最终总和：Σ(i*13%100) = 1548，-32*30 = 588
        expect("st-final-sum", st.query_sum(0, n - 1) == 588);
    }
    return g_fails;
}

} // namespace dt
} // namespace nefu
