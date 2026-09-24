# -*- coding: utf-8 -*-
# Replace btree delete_key with standard borrow/merge implementation
import io, sys

path = r"D:\mycppos1\nefuOS\core\datlib\btree.cpp"
with io.open(path, "r", encoding="utf-8") as f:
    src = f.read()

old_start = "// 删除：优先删叶子中的键；内部节点的键用后继（或前驱）替换后递归删除。"
old_end = "    n->child[i] = delete_key(n->child[i], key, removed);\n    return n;\n}"

si = src.index(old_start)
# find the end of the function: the closing brace after the old_end text
ei = src.index(old_end, si) + len(old_end)
# the closing brace of function is right after old_end (one more char after `return n;\n}` is included already)
old_block = src[si:ei]

new_block = """// ---- 删除（标准 B 树删除：下降时保证 child 至少 MIN+1 键，必要时借位/合并）----
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
}"""

assert src.count(old_start) == 1, "old_start count=%d" % src.count(old_start)
assert src.count(old_end) == 1, "old_end count=%d" % src.count(old_end)
src = src[:si] + new_block + src[ei:]
with io.open(path, "w", encoding="utf-8", newline="\n") as f:
    f.write(src)
print("REPLACED_OK", len(new_block))
