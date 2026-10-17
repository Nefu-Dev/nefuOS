// nefuOS stdcxx - internal <_avltree.h>
// A generic AVL tree carrying a per-key mapped value. Used by <map> and <set>.
// Header-only, allocates nodes on the nefuOS kernel heap. Not a public header.
#pragma once
#include "_core.h"
namespace nefu {
namespace stdx {

template <class Key, class V, class Compare>
class avltree {
public:
    struct Node {
        Key   key;
        V     val;
        Node* left;
        Node* right;
        Node* parent;
        int   h;
        Node(const Key& k) : key(k), val(), left(0), right(0), parent(0), h(1) {}
        Node(const Key& k, const V& v) : key(k), val(v), left(0), right(0), parent(0), h(1) {}
    };

    avltree() : root_(0), n_(0) {}
    ~avltree() { clear(root_); root_ = 0; n_ = 0; }

    size_t size() const { return n_; }
    bool empty() const { return n_ == 0; }
    Node* root() const { return root_; }

    Node* find(const Key& k) const {
        Node* x = root_;
        while (x) {
            if (cmp(k, x->key)) x = x->left;
            else if (cmp(x->key, k)) x = x->right;
            else return x;
        }
        return 0;
    }

    struct Ins { Node* node; bool inserted; };
    // If present, overwrite the value and return inserted=false.
    Ins insert(const Key& k, const V& v) {
        Node* z = new Node(k, v);
        bool inserted = insert_node(root_, 0, z);
        if (!inserted) {
            // duplicate: keep existing node, set its value, drop z
            Node* ex = find(k);
            ex->val = v;
            delete_node(z);
            return Ins{ ex, false };
        }
        ++n_;
        return Ins{ z, true };
    }

    bool erase(const Key& k) {
        Node* x = find(k);
        if (!x) return false;
        remove_node(x);
        --n_;
        return true;
    }

    Node* first() const { Node* x = root_; if (!x) return 0; while (x->left) x = x->left; return x; }
    Node* last() const  { Node* x = root_; if (!x) return 0; while (x->right) x = x->right; return x; }
    Node* next(Node* x) const {
        if (x->right) { x = x->right; while (x->left) x = x->left; return x; }
        while (x->parent && x == x->parent->right) x = x->parent;
        return x->parent;
    }
    Node* prev(Node* x) const {
        if (x->left) { x = x->left; while (x->right) x = x->right; return x; }
        while (x->parent && x == x->parent->left) x = x->parent;
        return x->parent;
    }

private:
    Node*  root_;
    size_t n_;
    Compare cmp;

    static int h(Node* x) { return x ? x->h : 0; }
    static void upd(Node* x) { x->h = (h(x->left) > h(x->right) ? h(x->left) : h(x->right)) + 1; }
    static int bf(Node* x) { return h(x->left) - h(x->right); }

    Node* rotate_left(Node* x) {
        Node* y = x->right; x->right = y->left;
        if (y->left) y->left->parent = x;
        y->parent = x->parent;
        if (x->parent) { if (x == x->parent->left) x->parent->left = y; else x->parent->right = y; }
        else root_ = y;
        y->left = x; x->parent = y;
        upd(x); upd(y);
        return y;
    }
    Node* rotate_right(Node* x) {
        Node* y = x->left; x->left = y->right;
        if (y->right) y->right->parent = x;
        y->parent = x->parent;
        if (x->parent) { if (x == x->parent->left) x->parent->left = y; else x->parent->right = y; }
        else root_ = y;
        y->right = x; x->parent = y;
        upd(x); upd(y);
        return y;
    }

    void rebalance(Node* x) {
        while (x) {
            upd(x);
            int b = bf(x);
            if (b > 1) { if (bf(x->left) < 0) rotate_left(x->left); x = rotate_right(x); }
            else if (b < -1) { if (bf(x->right) > 0) rotate_right(x->right); x = rotate_left(x); }
            x = x->parent;
        }
    }

    bool insert_node(Node*& x, Node* p, Node* z) {
        if (!x) { z->parent = p; x = z; return true; }
        if (cmp(z->key, x->key)) {
            bool ins = insert_node(x->left, x, z);
            if (ins) rebalance(x);
            return ins;
        }
        if (cmp(x->key, z->key)) {
            bool ins = insert_node(x->right, x, z);
            if (ins) rebalance(x);
            return ins;
        }
        return false;
    }

    void remove_node(Node* x) {
        if (x->left && x->right) {
            Node* s = x->right; while (s->left) s = s->left;
            Key tk = x->key; x->key = s->key; s->key = tk;
            V tv = x->val; x->val = s->val; s->val = tv;
            x = s;
        }
        Node* child = x->left ? x->left : x->right;
        Node* p = x->parent;
        if (child) child->parent = p;
        if (!p) root_ = child;
        else if (x == p->left) p->left = child;
        else p->right = child;
        delete_node(x);
        rebalance(p);
    }

    void clear(Node* x) { if (!x) return; clear(x->left); clear(x->right); delete_node(x); }
    void delete_node(Node* x) { delete x; }
};

} // namespace stdx
} // namespace nefu
