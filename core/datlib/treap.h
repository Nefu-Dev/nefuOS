// nefuOS data-types library — treap (randomized BST)
// 树堆：二叉搜索树 + 随机优先级堆性质。每个节点带随机优先级，
// 通过旋转保证"堆序"（父优先级 > 子），随机化使期望高度 O(log n)，
// 无需维护平衡因子，实现比 AVL/红黑树简单得多。
// 本实现支持插入/删除/查找/第 k 小/排名（size 域）。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace dt {

struct TPNode {
    int key;
    int prio;        // 随机优先级（大顶堆序）
    int val;
    int sz;          // 子树节点数（排名用）
    TPNode* l;
    TPNode* r;
};

class treap {
public:
    treap() : root_(0), rng_(0x9E3779B9u), size_(0) {}
    ~treap() { destroy(root_); root_ = 0; }
    treap(const treap&) = delete;
    treap& operator=(const treap&) = delete;

    int size() const { return size_; }
    bool empty() const { return root_ == 0; }

    // 插入（重复键覆盖值）；返回是否新键
    bool insert(int key, int val);
    bool find(int key, int& val) const;
    bool contains(int key) const;
    bool remove(int key);
    // 第 k 小（k 从 0 起）；不存在返回 false
    bool kth(int k, int& key, int& val) const;
    // 排名（小于 key 的元素个数 + 1）；key 不存在返回 0
    int rank(int key) const;
    int inorder(int* keys, int* vals, int cap) const;

private:
    TPNode* root_;
    unsigned rng_;
    int size_;

    unsigned next_rand() {
        // xorshift32 伪随机：快速且周期 2^32-1
        unsigned x = rng_;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        rng_ = x;
        return x;
    }
    static TPNode* new_node(int key, int val, unsigned prio) {
        TPNode* n = new TPNode;
        n->key = key; n->val = val; n->prio = (int)prio; n->sz = 1;
        n->l = 0; n->r = 0;
        return n;
    }
    static void destroy(TPNode* n) {
        if (!n) return;
        destroy(n->l); destroy(n->r);
        delete n;
    }
    static int sz(TPNode* n) { return n ? n->sz : 0; }
    static void upd(TPNode* n) { if (n) n->sz = 1 + sz(n->l) + sz(n->r); }
    // 旋转维持堆序
    static TPNode* rot_right(TPNode* n) {
        TPNode* x = n->l;
        n->l = x->r;
        x->r = n;
        upd(n); upd(x);
        return x;
    }
    static TPNode* rot_left(TPNode* n) {
        TPNode* x = n->r;
        n->r = x->l;
        x->l = n;
        upd(n); upd(x);
        return x;
    }
    static TPNode* insert_node(TPNode* n, int key, int val, unsigned prio, bool& is_new);
    static TPNode* remove_node(TPNode* n, int key, bool& removed);
    static void inorder_node(TPNode* n, int* keys, int* vals, int cap, int& k);
    static bool kth_node(TPNode* n, int k, int& key, int& val);
    static int rank_node(TPNode* n, int key);
};

int treap_self_test();


int treap_self_test();

} // namespace dt
} // namespace nefu
