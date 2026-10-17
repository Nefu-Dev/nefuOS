// nefuOS stdcxx - <set>
// Ordered set backed by an AVL tree (mapped value is unused, V = bool).
#pragma once
#include "_core.h"
#include "_avltree.h"
#include "functional.h"
namespace nefu {
namespace stdx {

template <class Key, class Compare = less<Key> >
class set {
    avltree<Key, bool, Compare> avl_;

public:
    typedef Key key_type;
    typedef size_t size_type;

    set() {}
    set(const set& o) { *this = o; }
    set& operator=(const set& o) { if (this != &o) { clear(); for (const_iterator it = o.begin(); it != o.end(); ++it) insert(*it); } return *this; }

    class iterator {
    public:
        typename avltree<Key, bool, Compare>::Node* n_;
        iterator() : n_(0) {}
        explicit iterator(typename avltree<Key, bool, Compare>::Node* n) : n_(n) {}
        iterator& operator++() { n_ = (n_ ? avltree<Key, bool, Compare>::next(n_) : 0); return *this; }
        iterator& operator--() { n_ = (n_ ? avltree<Key, bool, Compare>::prev(n_) : 0); return *this; }
        bool operator==(const iterator& o) const { return n_ == o.n_; }
        bool operator!=(const iterator& o) const { return n_ != o.n_; }
        const Key& operator*() const { return n_->key; }
        const Key* operator->() const { return &n_->key; }
    };
    typedef iterator const_iterator;

    iterator begin() const { return iterator(avl_.first()); }
    iterator end() const   { return iterator(0); }

    size_type size() const { return avl_.size(); }
    bool empty() const { return avl_.empty(); }
    void clear() { *this = set(); }
    bool contains(const Key& k) const { return avl_.find(k) != 0; }
    iterator find(const Key& k) const { return iterator(avl_.find(k)); }

    struct R { iterator first; bool second; };
    R insert(const Key& k) { typename avltree<Key, bool, Compare>::Ins r = avl_.insert(k, true); return R{ iterator(r.node), r.inserted }; }
    iterator erase(iterator it) { if (!it.n_) return end(); typename avltree<Key, bool, Compare>::Node* nx = avl_.next(it.n_); avl_.erase(it.n_->key); return iterator(nx); }
    size_type erase(const Key& k) { return avl_.erase(k) ? 1 : 0; }
};

} // namespace stdx
} // namespace nefu
