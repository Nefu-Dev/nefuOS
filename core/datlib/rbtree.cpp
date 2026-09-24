// nefuOS data-types library — rbtree implementation
#include "rbtree.h"
#include <stdio.h>

namespace nefu {
namespace dt {

// ---- 旋转：局部调整保持中序不变，用于修复失衡 ----

void rbtree::rotate_left(RBNode* x) {
    // 左旋：x 的右孩子 y 上提为父，x 成为 y 的左孩子
    RBNode* y = x->right;
    x->right = y->left;
    if (y->left) y->left->parent = x;
    y->parent = x->parent;
    if (!x->parent) root_ = y;
    else if (x == x->parent->left) x->parent->left = y;
    else x->parent->right = y;
    y->left = x;
    x->parent = y;
}

void rbtree::rotate_right(RBNode* x) {
    // 右旋：对称操作
    RBNode* y = x->left;
    x->left = y->right;
    if (y->right) y->right->parent = x;
    y->parent = x->parent;
    if (!x->parent) root_ = y;
    else if (x == x->parent->right) x->parent->right = y;
    else x->parent->left = y;
    y->right = x;
    x->parent = y;
}

// ---- 插入 ----

bool rbtree::insert(int key, int val) {
    RBNode* z = new_node(key, val, 1);   // 新节点先染红
    RBNode* y = 0;
    RBNode* x = root_;
    while (x) {                          // 普通 BST 插入
        y = x;
        if (key == x->key) { x->val = val; delete z; return false; }
        x = (key < x->key) ? x->left : x->right;
    }
    z->parent = y;
    if (!y) root_ = z;
    else if (key < y->key) y->left = z;
    else y->right = z;
    size_++;
    insert_fixup(z);
    return true;
}

void rbtree::insert_fixup(RBNode* z) {
    // 新节点红，若父也红则违反性质 4，需修复
    while (z->parent && z->parent->color == 1) {
        RBNode* p = z->parent;
        RBNode* g = p->parent;           // 祖父必存在（根是黑的）
        if (p == g->left) {
            RBNode* u = g->right;        // 叔叔
            if (u && u->color == 1) {
                // 情形 1：叔叔红 → 变色，问题上移两层
                p->color = 0; u->color = 0; g->color = 1;
                z = g;
            } else {
                if (z == p->right) {
                    // 情形 2：之字形 → 先左旋成直线
                    z = p;
                    rotate_left(z);
                    p = z->parent;
                }
                // 情形 3：直线 → 右旋 + 变色
                p->color = 0;
                if (p->parent) p->parent->color = 1;
                rotate_right(p->parent);
            }
        } else {
            // 对称：父是祖父的右孩子
            RBNode* u = g->left;
            if (u && u->color == 1) {
                p->color = 0; u->color = 0; g->color = 1;
                z = g;
            } else {
                if (z == p->left) {
                    z = p;
                    rotate_right(z);
                    p = z->parent;
                }
                p->color = 0;
                if (p->parent) p->parent->color = 1;
                rotate_left(p->parent);
            }
        }
    }
    root_->color = 0;                    // 根永远黑
}

// ---- 查找 ----

bool rbtree::find(int key, int& val) const {
    RBNode* n = search_node(root_, key);
    if (!n) return false;
    val = n->val;
    return true;
}

bool rbtree::contains(int key) const {
    return search_node(root_, key) != 0;
}

// ---- 删除（替身删除 + 双黑修正）----

RBNode* rbtree::tree_min(RBNode* n) const {
    while (n->left) n = n->left;
    return n;
}

bool rbtree::remove(int key) {
    RBNode* z = search_node(root_, key);
    if (!z) return false;
    RBNode* y = z;
    RBNode* x = 0;
    RBNode* xp = 0;
    int y_orig_color = y->color;
    if (!z->left) {
        // 只有右孩子（或叶子）：直接提升右孩子
        x = z->right;
        xp = z->parent;
        if (!z->parent) root_ = x;
        else if (z == z->parent->left) z->parent->left = x;
        else z->parent->right = x;
        if (x) x->parent = z->parent;
    } else if (!z->right) {
        x = z->left;
        xp = z->parent;
        if (!z->parent) root_ = x;
        else if (z == z->parent->left) z->parent->left = x;
        else z->parent->right = x;
        if (x) x->parent = z->parent;
    } else {
        // 两个孩子的替身：中序后继 y
        y = tree_min(z->right);
        y_orig_color = y->color;
        x = y->right;
        if (y->parent == z) {
            xp = y;
            if (x) x->parent = y;
        } else {
            xp = y->parent;
            if (!y->parent) { /* 不会发生 */ }
            if (y == y->parent->left) y->parent->left = x;
            else y->parent->right = x;
            if (x) x->parent = y->parent;
            y->right = z->right;
            z->right->parent = y;
        }
        // y 顶替 z 的位置
        y->parent = z->parent;
        if (!z->parent) root_ = y;
        else if (z == z->parent->left) z->parent->left = y;
        else z->parent->right = y;
        y->left = z->left;
        z->left->parent = y;
        y->color = z->color;
    }
    delete z;
    size_--;
    if (y_orig_color == 0) remove_fixup(x, xp);   // 删黑点 → 黑高失衡
    return true;
}

void rbtree::remove_fixup(RBNode* x, RBNode* xp) {
    // x 是"双黑"节点（被删黑点后补位）；循环把双黑沿树推上
    while (x != root_ && (!x || x->color == 0)) {
        if (!xp) break;
        if (x == xp->left) {
            RBNode* w = xp->right;       // 兄弟
            if (w && w->color == 1) {
                // 情形 1：兄弟红 → 变父红、兄弟黑、左旋
                w->color = 0; xp->color = 1;
                rotate_left(xp);
                w = xp->right;
            }
            if ((!w->left || w->left->color == 0) && (!w->right || w->right->color == 0)) {
                // 情形 2：兄弟两黑子 → 兄弟变红，双黑上移
                w->color = 1;
                x = xp;
                xp = x->parent;
            } else {
                if (!w->right || w->right->color == 0) {
                    // 情形 3：兄弟右黑 → 先右旋成情形 4
                    if (w->left) w->left->color = 0;
                    w->color = 1;
                    rotate_right(w);
                    w = xp->right;
                }
                // 情形 4：兄弟右红 → 旋转 + 收尾
                w->color = xp->color;
                xp->color = 0;
                if (w->right) w->right->color = 0;
                rotate_left(xp);
                x = root_;
                xp = 0;
            }
        } else {
            // 对称
            RBNode* w = xp->left;
            if (w && w->color == 1) {
                w->color = 0; xp->color = 1;
                rotate_right(xp);
                w = xp->left;
            }
            if ((!w->left || w->left->color == 0) && (!w->right || w->right->color == 0)) {
                w->color = 1;
                x = xp;
                xp = x->parent;
            } else {
                if (!w->left || w->left->color == 0) {
                    if (w->right) w->right->color = 0;
                    w->color = 1;
                    rotate_left(w);
                    w = xp->left;
                }
                w->color = xp->color;
                xp->color = 0;
                if (w->left) w->left->color = 0;
                rotate_right(xp);
                x = root_;
                xp = 0;
            }
        }
    }
    if (x) x->color = 0;
    if (root_) root_->color = 0;
}

// ---- 遍历与验证 ----

int rbtree::inorder(int* keys, int* vals, int cap) const {
    // 用显式栈中序遍历（无递归，避免深树爆栈）
    struct St { RBNode* n; int state; };   // state 0=去左 1=输出后去右
    St st[4096];
    int sp = 0;
    RBNode* cur = root_;
    int k = 0;
    for (;;) {
        if (cur) {
            st[sp].n = cur; st[sp].state = 0; sp++;
            cur = cur->left;
        } else if (sp > 0) {
            St& f = st[--sp];
            cur = f.n;
            if (f.state == 0) {
                if (k < cap) { keys[k] = cur->key; vals[k] = cur->val; k++; }
                f.state = 1; sp++;
                cur = cur->right;
            } else {
                cur = 0;         // state=1 已输出过，回到栈顶继续
            }
        } else break;
    }
    return k;
}

int rbtree::verify() const {
    int violations = 0;
    // 性质 2：根黑
    if (root_ && root_->color != 0) violations++;
    check_black(root_, violations);
    return violations;
}

int rbtree::check_black(RBNode* n, int& violations) {
    // 递归校验：返回黑高（到叶的黑节点数）；同时检查红不相连
    if (!n) return 1;
    int lh = check_black(n->left, violations);
    int rh = check_black(n->right, violations);
    if (lh != rh) violations++;            // 性质 5
    if (n->color == 1) {
        if (n->left && n->left->color == 1) violations++;    // 性质 4
        if (n->right && n->right->color == 1) violations++;
    }
    return n->color == 0 ? lh + 1 : lh;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int rbtree_self_test() {
    g_fails = 0;
    {
        rbtree t;
        expect("rb-empty", t.empty());
        // 顺序插入 1..12（经典退化序列，考验自平衡）
        bool ok = true;
        for (int i = 1; i <= 12; i++) if (!t.insert(i, i * 10)) ok = false;
        expect("rb-insert", ok && t.size() == 12);
        expect("rb-dup", !t.insert(6, 999));
        int v = 0;
        expect("rb-find", t.find(12, v) && v == 120);
        expect("rb-find2", t.find(1, v) && v == 10);
        expect("rb-contains", t.contains(7) && !t.contains(99));
        // 中序升序
        int ks[32], vs[32];
        int n = t.inorder(ks, vs, 32);
        expect("rb-inorder", n == 12);
        ok = (n == 12);
        for (int i = 0; i < n; i++) if (ks[i] != i + 1) ok = false;
        expect("rb-inorder-v", ok);
        // 结构合法
        expect("rb-verify", t.verify() == 0);
        // 黑高验证（检查属性已由 verify 覆盖）
        // 删除一半
        int removed = 0;
        for (int i = 2; i <= 12; i += 2) if (t.remove(i)) removed++;
        expect("rb-remove", removed == 6 && t.size() == 6);
        expect("rb-remove-gone", !t.contains(4) && t.contains(3));
        expect("rb-verify2", t.verify() == 0);
        // 随机插入删除保持平衡
        rbtree r2;
        for (int i = 0; i < 200; i++) r2.insert((i * 37) % 199, i);
        expect("rb-mass", r2.size() == 199);
        for (int i = 0; i < 199; i++) expect("rb-mass2", r2.contains((i * 37) % 199));
        expect("rb-verify3", r2.verify() == 0);
        // 删除到空
        while (r2.size() > 0) {
            int ks2[16]; int vs2[16];
            int nn = r2.inorder(ks2, vs2, 16);
            r2.remove(ks2[nn - 1]);
        }
        expect("rb-drain", r2.empty());
    }
    {
        // 全删黑树（只有左孩子链）
        rbtree t;
        t.insert(5, 1); t.insert(3, 1); t.insert(7, 1); t.insert(1, 1);
        expect("rb-d1", t.remove(5) && t.verify() == 0);
        expect("rb-d2", t.remove(3) && t.verify() == 0);
        expect("rb-d3", t.remove(7) && t.verify() == 0);
        expect("rb-d4", t.remove(1) && t.empty());
    }
    return g_fails;
}

} // namespace dt
} // namespace nefu
