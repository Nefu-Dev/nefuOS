// nefuOS dblib —— B+ 树实现 + 自测
// 教学重点：分裂（split）与借位/合并（borrow/merge）保持平衡。
#include "dblib/bptree.h"
#include <cstdio>
#include <algorithm>

namespace nefu {
namespace dbx {

// Node 现为公有嵌套类型：加别名使类外实现书写简洁
using Node = BpTree::Node;

BpTree::~BpTree() { destroy(root_); }

void BpTree::destroy(Node* n) {
    if (!n) return;
    if (!n->leaf)
        for (size_t i = 0; i < n->kids.size(); i++) destroy(n->kids[i]);
    delete n;
}

// ---- 插入 ----
void BpTree::split_child(Node* parent, int idx, Node* child) {
    // child 已满（keys.size()==ORDER），分裂为两个
    Node* right = new Node(child->leaf);
    right->parent = parent;
    int mid = ORDER / 2;          // 中位键位置
    int sep = child->keys[mid];   // 分隔键（先取走，稍后截断）
    // 右兄弟从 mid+1 开始（内部节点：分隔键上移；叶子：分隔键留在左叶子并复制给父）
    for (int i = mid + 1; i < (int)child->keys.size(); i++)
        right->keys.push_back(child->keys[i]);
    if (child->leaf) {
        for (int i = mid + 1; i < (int)child->vals.size(); i++)
            right->vals.push_back(child->vals[i]);
        right->next = child->next;
        child->next = right;
        // 左叶子含 keys[0..mid]（分隔键仍在叶子中，父只是副本）
        child->keys.resize(mid + 1);
        child->vals.resize(mid + 1);
    } else {
        for (int i = mid + 1; i < (int)child->kids.size(); i++) {
            right->kids.push_back(child->kids[i]);
            child->kids[i]->parent = right;
        }
        child->keys.resize(mid);
        child->kids.resize(mid + 1);
    }
    parent->keys.insert(parent->keys.begin() + idx, sep);
    parent->kids.insert(parent->kids.begin() + idx + 1, right);
}

Node* BpTree::insert_rec(Node* n, int key, int value, bool& ok) {
    // 叶子：直接插入有序
    if (n->leaf) {
        int i = 0;
        while (i < (int)n->keys.size() && n->keys[i] < key) i++;
        if (i < (int)n->keys.size() && n->keys[i] == key) {
            n->vals[i] = value;   // 更新
            ok = true;
            return 0;
        }
        n->keys.insert(n->keys.begin() + i, key);
        n->vals.insert(n->vals.begin() + i, value);
        ok = true;
        total_++;
        if ((int)n->keys.size() > ORDER) {
            // 叶子分裂（无父时创建新根）
            if (!n->parent) {
                Node* r = new Node(false);
                r->parent = 0;
                r->kids.push_back(n);
                n->parent = r;
                split_child(r, 0, n);
                return r;
            }
            split_child(n->parent, find_idx(n->parent, n), n);
            return 0;
        }
        return 0;
    }
    // 内部：找子树
    int i = 0;
    while (i < (int)n->keys.size() && n->keys[i] < key) i++;
    if (i < (int)n->keys.size() && n->keys[i] == key) {
        // 走到最左叶子更新
        Node* leaf = n->kids[i];
        while (!leaf->leaf) leaf = leaf->kids[0];
        int j = 0;
        while (j < (int)leaf->keys.size() && leaf->keys[j] < key) j++;
        if (j < (int)leaf->keys.size() && leaf->keys[j] == key) leaf->vals[j] = value;
        ok = true;
        return 0;
    }
    Node* ch = n->kids[i];
    Node* newroot = insert_rec(ch, key, value, ok);
    // 内部节点最多 ORDER-1 个键：溢出时分裂
    if ((int)n->keys.size() > ORDER - 1) {
        if (!n->parent) {
            Node* r = new Node(false);
            r->parent = 0;
            r->kids.push_back(n);
            n->parent = r;
            split_child(r, 0, n);
            return r;
        }
        split_child(n->parent, find_idx(n->parent, n), n);
        return 0;
    }
    return newroot;
}

int BpTree::find_idx(Node* parent, Node* child) {
    for (size_t i = 0; i < parent->kids.size(); i++)
        if (parent->kids[i] == child) return (int)i;
    return 0;
}

void BpTree::insert(int key, int value) {
    if (!root_) {
        root_ = new Node(true);
        total_ = 0;
    }
    bool ok = false;
    Node* nr = insert_rec(root_, key, value, ok);
    if (nr) root_ = nr;
}

bool BpTree::find(int key, int& value) const {
    if (!root_) return false;
    Node* n = root_;
    while (true) {
        int i = 0;
        while (i < (int)n->keys.size() && n->keys[i] < key) i++;
        if (n->leaf) {
            if (i < (int)n->keys.size() && n->keys[i] == key) { value = n->vals[i]; return true; }
            return false;
        }
        n = n->kids[i];
    }
}

// ---- 删除 ----
bool BpTree::remove_rec(Node* n, int key) {
    if (n->leaf) {
        int i = 0;
        while (i < (int)n->keys.size() && n->keys[i] < key) i++;
        if (i < (int)n->keys.size() && n->keys[i] == key) {
            n->keys.erase(n->keys.begin() + i);
            n->vals.erase(n->vals.begin() + i);
            total_--;
            return true;
        }
        return false;
    }
    int i = 0;
    while (i < (int)n->keys.size() && n->keys[i] < key) i++;
    bool found = remove_rec(n->kids[i], key);
    // 教学简化：不做欠额再平衡（delete 教学版本允许短暂下溢）
    return found;
}

bool BpTree::remove(int key) {
    if (!root_) return false;
    bool r = remove_rec(root_, key);
    if (r && total_ == 0) {
        destroy(root_);
        root_ = 0;
    }
    return r;
}

// ---- 查询 ----
void BpTree::collect(Node* n, std::vector<std::pair<int, int> >& out) const {
    if (!n) return;
    if (n->leaf) {
        for (size_t i = 0; i < n->keys.size(); i++)
            out.push_back(std::make_pair(n->keys[i], n->vals[i]));
        return;
    }
    for (size_t i = 0; i < n->kids.size(); i++) collect(n->kids[i], out);
}

std::vector<std::pair<int, int> > BpTree::all() const {
    std::vector<std::pair<int, int> > out;
    collect(root_, out);
    return out;
}

std::vector<std::pair<int, int> > BpTree::range(int lo, int hi) const {
    std::vector<std::pair<int, int> > allp = all();
    std::vector<std::pair<int, int> > out;
    for (size_t i = 0; i < allp.size(); i++)
        if (allp[i].first >= lo && allp[i].first <= hi) out.push_back(allp[i]);
    return out;
}

int BpTree::count_rec(Node* n) const {
    if (!n) return 0;
    if (n->leaf) return (int)n->keys.size();
    int c = 0;
    for (size_t i = 0; i < n->kids.size(); i++) c += count_rec(n->kids[i]);
    return c;
}

int BpTree::count() const { return total_; }

int BpTree::height_rec(Node* n) const {
    if (!n) return 0;
    int h = 1;
    Node* p = n;
    while (!p->leaf) { p = p->kids[0]; h++; }
    return h;
}

int BpTree::height() const { return height_rec(root_); }

// ---- self test ----
int BpTree::self_test() {
    int fails = 0;
    // 1. 插入与查找
    {
        BpTree t;
        for (int i = 1; i <= 100; i++) t.insert(i, i * 10);
        int v = 0;
        if (!t.find(1, v) || v != 10) fails++;
        if (!t.find(100, v) || v != 1000) fails++;
        if (t.find(101, v)) fails++;
        if (t.count() != 100) fails++;
        if (t.height() > 4) fails++;   // 阶 4，100 键树高应 <= 4
    }
    // 2. 乱序插入仍有序
    {
        BpTree t;
        int keys[10] = { 5, 3, 8, 1, 9, 2, 7, 4, 6, 0 };
        for (int i = 0; i < 10; i++) t.insert(keys[i], keys[i] + 100);
        std::vector<std::pair<int, int> > a = t.all();
        if (a.size() != 10) fails++;
        for (int i = 1; i < (int)a.size(); i++)
            if (a[i].first <= a[i - 1].first) fails++;
        if (a[0].first != 0 || a[9].first != 9) fails++;
    }
    // 3. 更新
    {
        BpTree t;
        t.insert(5, 1);
        t.insert(5, 2);
        int v = 0;
        t.find(5, v);
        if (v != 2) fails++;
        if (t.count() != 1) fails++;
    }
    // 4. 删除
    {
        BpTree t;
        for (int i = 0; i < 20; i++) t.insert(i, i);
        if (!t.remove(7)) fails++;
        int v = 0;
        if (t.find(7, v)) fails++;
        if (t.remove(7)) fails++;   // 再次删除应失败
        if (t.count() != 19) fails++;
        if (!t.remove(0) || !t.remove(19)) fails++;
        if (t.count() != 17) fails++;
    }
    // 5. 范围查询
    {
        BpTree t;
        for (int i = 0; i < 50; i++) t.insert(i * 2, i);
        std::vector<std::pair<int, int> > r = t.range(10, 30);
        // 键 10,12,...,30 共 11 个
        if (r.size() != 11) fails++;
        if (r[0].first != 10 || r[10].first != 30) fails++;
    }
    // 6. 全删为空
    {
        BpTree t;
        t.insert(1, 1);
        t.remove(1);
        if (t.count() != 0) fails++;
    }
    return fails;
}

} // namespace dbx
} // namespace nefu
