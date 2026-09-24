// nefuOS dblib —— B+ 树 bptree
// 教学版：B+ 树（叶子存值、内部存键）插入/查找/删除 + 范围查询。
#pragma once
#include <vector>
#include <utility>

namespace nefu {
namespace dbx {

// B+ 树：int 键 -> int 值；阶数为 ORDER（最多 ORDER 个键，内部最多 ORDER-1）
class BpTree {
public:
    // 节点（公有：便于教学演示与类外实现）
    struct Node {
        bool leaf;
        std::vector<int> keys;
        std::vector<int> vals;            // 仅叶子
        std::vector<Node*> kids;          // 仅内部
        Node* next;                       // 叶子链
        Node* parent;
        Node(bool l) : leaf(l), next(0), parent(0) {}
    };

    BpTree() : root_(0) {}
    ~BpTree();

    // 插入/更新键值
    void insert(int key, int value);
    // 查找（找到返回 true）
    bool find(int key, int& value) const;
    // 删除（找到并删除返回 true）
    bool remove(int key);
    // 范围查询 [lo, hi] 的键值对
    std::vector<std::pair<int, int> > range(int lo, int hi) const;
    // 全部键值对（中序遍历叶子）
    std::vector<std::pair<int, int> > all() const;
    // 键总数
    int count() const;
    // 树高
    int height() const;

    // ---- self test ----
    static int self_test();

private:
    static const int ORDER = 4;   // 阶数（小阶便于教学演示）
    Node* root_;
    int total_;

    void destroy(Node* n);
    // 插入核心（返回分裂出的新根或 0）
    Node* insert_rec(Node* n, int key, int value, bool& ok);
    // 分裂
    void split_child(Node* parent, int idx, Node* child);
    // 删除核心
    bool remove_rec(Node* n, int key);
    void collect(Node* n, std::vector<std::pair<int, int> >& out) const;
    int count_rec(Node* n) const;
    int height_rec(Node* n) const;
    int find_idx(Node* parent, Node* child);
};

} // namespace dbx
} // namespace nefu
