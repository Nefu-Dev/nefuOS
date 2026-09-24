// nefuOS data-types library — btree implementation
#include "btree.h"
#include <stdio.h>

namespace nefu {
namespace dt {

// 分裂 x 的第 i 个孩子（该孩子已满）：把中间键提升到 x，
// 孩子一分为二。前提：x 未满，child[i] 满（nkeys == BT_MAX_KEY）。
void btree::split_child(BTNode* x, int i) {
    BTNode* y = x->child[i];
    BTNode* z = new_node(y->leaf);
    z->nkeys = BT_ORDER / 2 - 1;                 // 1（M=4 时取中）
    // 后一半键搬给 z
    for (int j = 0; j < z->nkeys; j++) {
        z->keys[j] = y->keys[j + BT_ORDER / 2];
        z->vals[j] = y->vals[j + BT_ORDER / 2];
    }
    if (!y->leaf) {
        for (int j = 0; j < BT_ORDER / 2; j++)
            z->child[j] = y->child[j + BT_ORDER / 2];
    }
    y->nkeys = BT_ORDER / 2 - 1;
    // x 腾位插入 z，并把中间键上提
    for (int j = x->nkeys; j > i; j--) {
        x->keys[j] = x->keys[j - 1];
        x->vals[j] = x->vals[j - 1];
        x->child[j + 1] = x->child[j];
    }
    x->child[i + 1] = z;
    x->keys[i] = y->keys[BT_ORDER / 2 - 1];
    x->vals[i] = y->vals[BT_ORDER / 2 - 1];
    x->nkeys++;
}

BTNode* btree::insert_nonfull(BTNode* n, int key, int val, bool& is_new) {
    int i = n->nkeys - 1;
    if (n->leaf) {
        // 叶子：直接插入（保持有序）
        while (i >= 0 && key < n->keys[i]) {
            n->keys[i + 1] = n->keys[i];
            n->vals[i + 1] = n->vals[i];
            i--;
        }
        if (i >= 0 && n->keys[i] == key) {       // 重复键：覆盖
            n->vals[i] = val;
            is_new = false;
            return n;
        }
        n->keys[i + 1] = key;
        n->vals[i + 1] = val;
        n->nkeys++;
        is_new = true;
        return n;
    }
    // 内部节点：找孩子下钻，必要时先分裂满孩子
    i = 0;
    while (i < n->nkeys && key > n->keys[i]) i++;
    if (i < n->nkeys && key == n->keys[i]) {
        n->vals[i] = val;                        // 重复键：覆盖
        is_new = false;
        return n;
    }
    if (n->child[i]->nkeys == BT_MAX_KEY) {
        split_child(n, i);
        if (key > n->keys[i]) i++;
    }
    n->child[i] = insert_nonfull(n->child[i], key, val, is_new);
    return n;
}

// ---- 删除（标准 B 树删除：下降时保证 child 至少 MIN+1 键，必要时借位/合并）----
namespace {
// 把 keys[i] 与 child[i+1] 合并进 child[i]；返回合并后的节点（原 child[i]）
BTNode* bt_merge(BTNode* n, int i) {
    BTNode* left = n->child[i];
    BTNode* right = n->child[i + 1];
    left->keys[left->nkeys] = n->keys[i];
    left->vals[left->nkeys] = n->vals[i];
    left->nkeys++;
    if (!left->leaf) left->child[left->nkeys] = right->child[0];
    for (int j = 0; j < right->nkeys; j++) {
        left->keys[left->nkeys] = right->keys[j];
        left->vals[left->nkeys] = right->vals[j];
        left->nkeys++;
        if (!left->leaf) left->child[left->nkeys] = right->child[j + 1];
    }
    delete right;
    for (int j = i; j < n->nkeys - 1; j++) {
        n->keys[j] = n->keys[j + 1];
        n->vals[j] = n->vals[j + 1];
    }
    for (int j = i + 1; j < n->nkeys; j++) n->child[j] = n->child[j + 1];
    n->nkeys--;
    return left;
}
} // namespace

// 删除：定位键；内部节点键用前驱/后继替换；递归前若 child 恰好 MIN 键则先借或合并。
BTNode* btree::delete_key(BTNode* n, int key, bool& removed) {
    int i = 0;
    while (i < n->nkeys && key > n->keys[i]) i++;

    if (i < n->nkeys && key == n->keys[i]) {
        if (n->leaf) {
            // 叶子直接删
            for (int j = i; j < n->nkeys - 1; j++) {
                n->keys[j] = n->keys[j + 1];
                n->vals[j] = n->vals[j + 1];
            }
            n->nkeys--;
            removed = true;
            return n;
        }
        // 内部节点键：优先用左子树最大键（前驱）替换
        if (n->child[i]->nkeys > BT_MIN_KEY) {
            BTNode* pred = n->child[i];
            while (!pred->leaf) pred = pred->child[pred->nkeys];
            n->keys[i] = pred->keys[pred->nkeys - 1];
            n->vals[i] = pred->vals[pred->nkeys - 1];
            n->child[i] = delete_key(n->child[i], n->keys[i], removed);
        } else if (n->child[i + 1]->nkeys > BT_MIN_KEY) {
            // 左子树不够 → 用右子树最小键（后继）替换
            BTNode* succ = n->child[i + 1];
            while (!succ->leaf) succ = succ->child[0];
            n->keys[i] = succ->keys[0];
            n->vals[i] = succ->vals[0];
            n->child[i + 1] = delete_key(n->child[i + 1], n->keys[i], removed);
        } else {
            // 两子树都 MIN → 合并 child[i] 与 child[i+1]（键下移）后递归删
            BTNode* merged = bt_merge(n, i);
            merged = delete_key(merged, key, removed);
            n->child[i] = merged;
        }
        return n;
    }

    if (n->leaf) return n;                       // 不存在

    // 递归前保证 child[i] 至少 MIN+1 键（否则删除后会欠填充）
    if (n->child[i]->nkeys == BT_MIN_KEY) {
        if (i > 0 && n->child[i - 1]->nkeys > BT_MIN_KEY) {
            // 从左兄弟借：sib 最大键升父，父键降 child[i]
            BTNode* c = n->child[i];
            BTNode* sib = n->child[i - 1];
            for (int j = c->nkeys; j > 0; j--) {
                c->keys[j] = c->keys[j - 1];
                c->vals[j] = c->vals[j - 1];
            }
            if (!c->leaf) for (int j = c->nkeys + 1; j > 0; j--) c->child[j] = c->child[j - 1];
            c->keys[0] = n->keys[i - 1];
            c->vals[0] = n->vals[i - 1];
            if (!c->leaf) c->child[0] = sib->child[sib->nkeys];
            n->keys[i - 1] = sib->keys[sib->nkeys - 1];
            n->vals[i - 1] = sib->vals[sib->nkeys - 1];
            sib->nkeys--;
            c->nkeys++;
        } else if (i < n->nkeys && n->child[i + 1]->nkeys > BT_MIN_KEY) {
            // 从右兄弟借：父键降 child[i]，sib 最小键升父
            BTNode* c = n->child[i];
            BTNode* sib = n->child[i + 1];
            c->keys[c->nkeys] = n->keys[i];
            c->vals[c->nkeys] = n->vals[i];
            if (!c->leaf) c->child[c->nkeys + 1] = sib->child[0];
            n->keys[i] = sib->keys[0];
            n->vals[i] = sib->vals[0];
            for (int j = 0; j < sib->nkeys - 1; j++) {
                sib->keys[j] = sib->keys[j + 1];
                sib->vals[j] = sib->vals[j + 1];
            }
            if (!sib->leaf) for (int j = 0; j < sib->nkeys; j++) sib->child[j] = sib->child[j + 1];
            sib->nkeys--;
            c->nkeys++;
        } else {
            // 兄弟都不富余 → 合并（优先与右兄弟；i==nkeys 时与左兄弟）
            if (i < n->nkeys) {
                BTNode* merged = bt_merge(n, i);
                n->child[i] = merged;
            } else {
                BTNode* merged = bt_merge(n, i - 1);
                n->child[i - 1] = merged;
                i = i - 1;
            }
        }
    }
    n->child[i] = delete_key(n->child[i], key, removed);
    return n;
}

bool btree::insert(int key, int val) {
    bool is_new = true;
    if (!root_) {
        root_ = new_node(true);
        root_->keys[0] = key; root_->vals[0] = val;
        root_->nkeys = 1;
        size_ = 1;
        return true;
    }
    if (root_->nkeys == BT_MAX_KEY) {
        // 根满 → 新建根并分裂
        BTNode* s = new_node(false);
        s->child[0] = root_;
        split_child(s, 0);
        root_ = s;
    }
    root_ = insert_nonfull(root_, key, val, is_new);
    if (is_new) size_++;
    return is_new;
}

bool btree::find(int key, int& val) const {
    BTNode* n = search_node(root_, key);
    if (!n) return false;
    int i = 0;
    while (n->keys[i] != key) i++;
    val = n->vals[i];
    return true;
}

bool btree::contains(int key) const {
    return search_node(root_, key) != 0;
}

bool btree::remove(int key) {
    bool removed = false;
    if (root_) {
        root_ = delete_key(root_, key, removed);
        // 根变空则收掉（叶子直接置空；内部节点下移唯一孩子）
        if (root_ && root_->nkeys == 0) {
            BTNode* old = root_;
            root_ = old->leaf ? 0 : old->child[0];
            delete old;
        }
    }
    if (removed) size_--;
    return removed;
}

void btree::inorder_node(BTNode* n, int* keys, int* vals, int cap, int& k) {
    if (!n || k >= cap) return;
    for (int i = 0; i < n->nkeys; i++) {
        if (!n->leaf) inorder_node(n->child[i], keys, vals, cap, k);
        if (k < cap) { keys[k] = n->keys[i]; vals[k] = n->vals[i]; k++; }
    }
    if (!n->leaf) inorder_node(n->child[n->nkeys], keys, vals, cap, k);
}

int btree::inorder(int* keys, int* vals, int cap) const {
    int k = 0;
    inorder_node(root_, keys, vals, cap, k);
    return k;
}

int btree::height_node(BTNode* n) {
    if (!n) return 0;
    if (n->leaf) return 1;
    return 1 + height_node(n->child[0]);
}

int btree::height() const { return height_node(root_); }

// 临时调试：层序转储
void btree::debug_dump() const {
    if (!root_) { printf("(empty)\n"); return; }
    BTNode* level[1024];
    int cnt = 0;
    level[cnt++] = root_;
    while (cnt > 0) {
        int nxt = 0;
        BTNode* nlv[1024];
        for (int i = 0; i < cnt; i++) {
            BTNode* n = level[i];
            printf("[");
            for (int j = 0; j < n->nkeys; j++) printf("%s%d", j ? "," : "", n->keys[j]);
            printf("]%s ", n->leaf ? "L" : "I");
            if (!n->leaf) {
                for (int j = 0; j <= n->nkeys; j++) nlv[nxt++] = n->child[j];
            }
        }
        printf("\n");
        cnt = nxt;
        for (int i = 0; i < cnt; i++) level[i] = nlv[i];
    }
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int btree_self_test() {
    g_fails = 0;
    {
        btree t;
        expect("bt-empty", t.empty());
        // 插入 0..29（触发多次分裂、根分裂）
        bool ok = true;
        for (int i = 0; i < 30; i++) if (!t.insert(i, i * 2)) ok = false;
        expect("bt-insert", ok && t.size() == 30);
        int v = 0;
        expect("bt-find", t.find(0, v) && v == 0);
        expect("bt-find2", t.find(29, v) && v == 58);
        expect("bt-find3", t.find(15, v) && v == 30);
        expect("bt-contains", t.contains(7) && !t.contains(100));
        expect("bt-height", t.height() <= 4);     // 30 键 M=4 高度应 <= 3
        // 中序升序
        int ks[64], vs[64];
        int n = t.inorder(ks, vs, 64);
        ok = (n == 30);
        for (int i = 0; i < n; i++) if (ks[i] != i) ok = false;
        expect("bt-inorder", ok);
        // 覆盖
        expect("bt-dup", !t.insert(15, 999));
        expect("bt-dup-v", t.find(15, v) && v == 999);
        // 删除叶子键与内部键
        expect("bt-remove", t.remove(0));
        expect("bt-remove2", !t.remove(0));
        expect("bt-remove-leaf", t.remove(29));
        expect("bt-remove-int", t.remove(15));
        expect("bt-gone", !t.contains(15) && !t.contains(0));
        // 剩余仍完整有序：1..29 去掉 15 = 27 个
        n = t.inorder(ks, vs, 64);
        expect("bt-remain", n == 27);
        ok = (n == 27);
        for (int i = 0; i < n; i++) {
            int want = (i < 14) ? i + 1 : i + 2;    // 跳过 15
            if (ks[i] != want) ok = false;
        }
        expect("bt-remain-v", ok);
        // 全删
        for (int i = 1; i <= 29; i++) t.remove(i);
        expect("bt-drain", t.empty());
    }
    {
        // 大序列 + 交替删除
        btree t;
        for (int i = 0; i < 300; i++) t.insert((i * 37) % 300, i);
        expect("bt-mass", t.size() == 300);
        bool ok = true;
        for (int i = 0; i < 300; i++) if (!t.contains((i * 37) % 300)) ok = false;
        expect("bt-mass-v", ok);
        for (int i = 0; i < 300; i += 2) t.remove((i * 37) % 300);
        ok = true;
        for (int i = 0; i < 300; i += 2) if (t.contains((i * 37) % 300)) ok = false;
        for (int i = 1; i < 300; i += 2) if (!t.contains((i * 37) % 300)) ok = false;
        expect("bt-mass-del", ok);
    }
    return g_fails;
}

} // namespace dt
} // namespace nefu
