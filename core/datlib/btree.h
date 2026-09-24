// nefuOS data-types library — B-tree (btree, order-4 demo)
// B 树：多路平衡搜索树，节点可含多个键，广泛用于数据库与文件系统
// （磁盘块对应节点，降低 IO 次数）。本实现为教学版：
//   - 阶 M=4（每个节点最多 3 个键、4 个孩子，最少 1 个键）；
//   - 分裂式插入（自顶向下分裂满节点，避免回溯）；
//   - 合并式删除（简化：找到键则删，必要时借/合并——本版对删除做
//     降级处理：只支持内部节点的键替换为后继后删除，不做底层合并，
//     保持实现可读；删除后允许欠填充，但不破坏查找正确性）。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace dt {

const int BT_ORDER = 4;             // 阶：节点最多 4 个孩子
const int BT_MAX_KEY = BT_ORDER - 1; // 3
const int BT_MIN_KEY = 1;

struct BTNode {
    int nkeys;                      // 当前键数
    int keys[BT_MAX_KEY];
    int vals[BT_MAX_KEY];
    BTNode* child[BT_ORDER];        // child[i] 存 < keys[i]；child[nkeys] 存 > 最后键
    bool leaf;
};

class btree {
public:
    btree() : root_(0), size_(0) {}
    ~btree() { destroy(root_); root_ = 0; }
    btree(const btree&) = delete;
    btree& operator=(const btree&) = delete;

    int size() const { return size_; }
    bool empty() const { return root_ == 0; }

    bool insert(int key, int val);
    bool find(int key, int& val) const;
    bool contains(int key) const;
    bool remove(int key);
    int inorder(int* keys, int* vals, int cap) const;
    // 树高（根=1）；空树 0
    int height() const;
    // 临时调试：层序转储节点键
    void debug_dump() const;

private:
    BTNode* root_;
    int size_;

    static BTNode* new_node(bool leaf) {
        BTNode* n = new BTNode;
        n->nkeys = 0; n->leaf = leaf;
        for (int i = 0; i < BT_ORDER; i++) n->child[i] = 0;
        return n;
    }
    static void destroy(BTNode* n) {
        if (!n) return;
        if (!n->leaf) {
            for (int i = 0; i <= n->nkeys; i++) destroy(n->child[i]);
        }
        delete n;
    }
    static BTNode* search_node(BTNode* n, int key) {
        int i = 0;
        while (i < n->nkeys && key > n->keys[i]) i++;
        if (i < n->nkeys && key == n->keys[i]) return n;
        if (n->leaf) return 0;
        return search_node(n->child[i], key);
    }
    static void split_child(BTNode* x, int i);
    static BTNode* insert_nonfull(BTNode* n, int key, int val, bool& is_new);
    static BTNode* delete_key(BTNode* n, int key, bool& removed);
    static void inorder_node(BTNode* n, int* keys, int* vals, int cap, int& k);
    static int height_node(BTNode* n);
};

int btree_self_test();

} // namespace dt
} // namespace nefu
