// nefuOS data-types library — red-black tree (rbtree)
// 红黑树：自平衡二叉搜索树，保证插入/删除/查找 O(log n)。
// 规则（CLRS 版）：
//   1. 每个节点是红色或黑色；
//   2. 根是黑色；
//   3. 每个叶（NIL）是黑色；
//   4. 红色节点的两个子节点都是黑色（红不相连）；
//   5. 从任意节点到其所有后代叶的路径含相同数目的黑节点（黑高相等）。
// 插入后通过"变色 + 左旋/右旋"恢复平衡；删除用替身 + 双黑修正。
// 本实现以 int 为键、int 为值，教学注释标注每个旋转的用途。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace dt {

struct RBNode {
    int key;
    int val;
    int color;       // 0=黑 1=红
    RBNode* left;
    RBNode* right;
    RBNode* parent;
};

class rbtree {
public:
    rbtree() : root_(0), size_(0) {}
    ~rbtree() { destroy(root_); root_ = 0; }
    rbtree(const rbtree&) = delete;
    rbtree& operator=(const rbtree&) = delete;

    int size() const { return size_; }
    bool empty() const { return root_ == 0; }

    // 插入（重复键覆盖值）；返回是否新键
    bool insert(int key, int val);
    // 查找；找到返回 true 并写 val
    bool find(int key, int& val) const;
    bool contains(int key) const;
    // 删除；返回是否删除成功
    bool remove(int key);
    // 中序遍历（升序）到 out；返回节点数（out 容量 cap）
    int inorder(int* keys, int* vals, int cap) const;
    // 验证红黑性质；返回违规数（0 = 结构合法）
    int verify() const;

private:
    RBNode* root_;
    int size_;

    static RBNode* new_node(int key, int val, int color) {
        RBNode* n = new RBNode;
        n->key = key; n->val = val; n->color = color;
        n->left = 0; n->right = 0; n->parent = 0;
        return n;
    }
    static void destroy(RBNode* n) {
        if (!n) return;
        destroy(n->left);
        destroy(n->right);
        delete n;
    }
    static RBNode* search_node(RBNode* n, int key) {
        while (n) {
            if (key == n->key) return n;
            n = (key < n->key) ? n->left : n->right;
        }
        return 0;
    }
    void rotate_left(RBNode* x);
    void rotate_right(RBNode* x);
    void insert_fixup(RBNode* z);
    void remove_fixup(RBNode* x, RBNode* xp);
    RBNode* tree_min(RBNode* n) const;
    static int check_black(RBNode* n, int& violations);   // 返回黑高
};

int rbtree_self_test();

} // namespace dt
} // namespace nefu
