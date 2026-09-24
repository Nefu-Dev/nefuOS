// nefuOS data-types library — treap implementation
#include "treap.h"
#include <stdio.h>

namespace nefu {
namespace dt {

TPNode* treap::insert_node(TPNode* n, int key, int val, unsigned prio, bool& is_new) {
    if (!n) { is_new = true; return new_node(key, val, prio); }
    if (key == n->key) { n->val = val; is_new = false; return n; }   // 覆盖
    if (key < n->key) {
        n->l = insert_node(n->l, key, val, prio, is_new);
        if (n->l->prio > n->prio) n = rot_right(n);   // 堆序破坏 → 右旋
    } else {
        n->r = insert_node(n->r, key, val, prio, is_new);
        if (n->r->prio > n->prio) n = rot_left(n);
    }
    upd(n);
    return n;
}

TPNode* treap::remove_node(TPNode* n, int key, bool& removed) {
    if (!n) return 0;
    if (key == n->key) {
        removed = true;
        if (!n->l) { TPNode* r = n->r; delete n; return r; }
        if (!n->r) { TPNode* l = n->l; delete n; return l; }
        // 两个孩子：把优先级高的旋转上来，再递归删除
        if (n->l->prio > n->r->prio) {
            n = rot_right(n);
            n->r = remove_node(n->r, key, removed);
        } else {
            n = rot_left(n);
            n->l = remove_node(n->l, key, removed);
        }
    } else if (key < n->key) {
        n->l = remove_node(n->l, key, removed);
    } else {
        n->r = remove_node(n->r, key, removed);
    }
    upd(n);
    return n;
}

void treap::inorder_node(TPNode* n, int* keys, int* vals, int cap, int& k) {
    if (!n || k >= cap) return;
    inorder_node(n->l, keys, vals, cap, k);
    if (k < cap) { keys[k] = n->key; vals[k] = n->val; k++; }
    inorder_node(n->r, keys, vals, cap, k);
}

bool treap::kth_node(TPNode* n, int k, int& key, int& val) {
    if (!n) return false;
    int ls = sz(n->l);
    if (k < ls) return kth_node(n->l, k, key, val);
    if (k == ls) { key = n->key; val = n->val; return true; }
    return kth_node(n->r, k - ls - 1, key, val);
}

int treap::rank_node(TPNode* n, int key) {
    // 返回小于 key 的个数
    if (!n) return 0;
    if (key <= n->key) return rank_node(n->l, key);
    return 1 + sz(n->l) + rank_node(n->r, key);
}

bool treap::insert(int key, int val) {
    bool is_new = true;
    root_ = insert_node(root_, key, val, next_rand(), is_new);
    if (is_new) size_++;
    return is_new;
}

bool treap::find(int key, int& val) const {
    TPNode* n = root_;
    while (n) {
        if (key == n->key) { val = n->val; return true; }
        n = (key < n->key) ? n->l : n->r;
    }
    return false;
}

bool treap::contains(int key) const {
    TPNode* n = root_;
    while (n) {
        if (key == n->key) return true;
        n = (key < n->key) ? n->l : n->r;
    }
    return false;
}

bool treap::remove(int key) {
    bool removed = false;
    root_ = remove_node(root_, key, removed);
    if (removed) size_--;
    return removed;
}

bool treap::kth(int k, int& key, int& val) const {
    if (k < 0 || k >= size_) return false;
    return kth_node(root_, k, key, val);
}

int treap::rank(int key) const {
    // key 不存在时返回 0（区别于存在的排名 >= 1）
    if (!contains(key)) return 0;
    return rank_node(root_, key) + 1;
}

int treap::inorder(int* keys, int* vals, int cap) const {
    int k = 0;
    inorder_node(root_, keys, vals, cap, k);
    return k;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int treap_self_test() {
    g_fails = 0;
    {
        treap t;
        // 插入 0..49（顺序插入仍随机平衡）
        bool ok = true;
        for (int i = 0; i < 50; i++) if (!t.insert(i, i * 3)) ok = false;
        expect("tp-insert", ok && t.size() == 50);
        expect("tp-dup", !t.insert(25, 1));
        int v = 0;
        expect("tp-find", t.find(49, v) && v == 147);
        expect("tp-contains", t.contains(0) && !t.contains(-1));
        // 中序升序
        int ks[64], vs[64];
        int n = t.inorder(ks, vs, 64);
        ok = (n == 50);
        for (int i = 0; i < n; i++) if (ks[i] != i) ok = false;
        expect("tp-inorder", ok);
        // 第 k 小与排名
        int kk, kv;
        expect("tp-kth", t.kth(0, kk, kv) && kk == 0 && kv == 0);
        expect("tp-kth2", t.kth(49, kk, kv) && kk == 49);
        expect("tp-kth-none", !t.kth(50, kk, kv));
        expect("tp-rank", t.rank(10) == 11);
        expect("tp-rank-none", t.rank(100) == 0);
        // 删除
        expect("tp-remove", t.remove(10));
        expect("tp-remove2", !t.remove(10));
        expect("tp-remove-gone", !t.contains(10));
        n = t.inorder(ks, vs, 64);
        expect("tp-after-del", n == 49 && ks[9] == 9 && ks[10] == 11);
        // 全部删光
        for (int i = 0; i < 50; i++) t.remove(i);
        expect("tp-drain", t.empty());
    }
    {
        // 大量随机（验证结构稳定性）
        treap t;
        for (int i = 0; i < 500; i++) {
            int key = (i * 2654435761u) % 1000;   // 黄金分割散列
            t.insert(key, i);
        }
        // 再查全部
        bool ok = true;
        for (int i = 0; i < 500; i++) {
            int key = (i * 2654435761u) % 1000;
            if (!t.contains(key)) ok = false;
        }
        expect("tp-mass", ok);
        // 排名单调性：对存在的键，排名 = 排序后位置 + 1
        ok = true;
        int ks2[512], vs2[512];
        int kn = t.inorder(ks2, vs2, 512);
        for (int i = 0; i < kn; i++) {
            if (t.rank(ks2[i]) != i + 1) ok = false;
        }
        expect("tp-rank-mono", ok && kn == 500);
    }
    return g_fails;
}

} // namespace dt
} // namespace nefu
