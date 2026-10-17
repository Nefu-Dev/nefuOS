// nefuOS stdcxx - <map>
// Ordered key/value map backed by an AVL tree on the nefuOS kernel heap.
#pragma once
#include "_core.h"
#include "_avltree.h"
#include "functional.h"
#include "utility.h"
namespace nefu {
namespace stdx {

template <class Key, class T, class Compare = less<Key> >
class map {
    avltree<Key, T, Compare> avl_;

public:
    typedef Key key_type;
    typedef T mapped_type;
    typedef pair<const Key, T> value_type;
    typedef size_t size_type;

    map() {}
    map(const map& o) { *this = o; }
    map& operator=(const map& o) { if (this != &o) { clear(); for (const_iterator it = o.begin(); it != o.end(); ++it) (*this)[it->first] = it->second; } return *this; }

    class iterator {
    public:
        typename avltree<Key, T, Compare>::Node* n_;
        iterator() : n_(0) {}
        explicit iterator(typename avltree<Key, T, Compare>::Node* n) : n_(n) {}
        iterator& operator++() { n_ = (n_ ? avltree<Key, T, Compare>::next(n_) : 0); return *this; }
        iterator& operator--() { n_ = (n_ ? avltree<Key, T, Compare>::prev(n_) : 0); return *this; }
        bool operator==(const iterator& o) const { return n_ == o.n_; }
        bool operator!=(const iterator& o) const { return n_ != o.n_; }
        value_type operator*() const { return value_type(n_->key, n_->val); }
    };
    typedef iterator const_iterator;

    iterator begin() const { return iterator(avl_.first()); }
    iterator end() const   { return iterator(0); }

    size_type size() const { return avl_.size(); }
    bool empty() const { return avl_.empty(); }
    void clear() { *this = map(); }
    bool contains(const Key& k) const { return avl_.find(k) != 0; }
    iterator find(const Key& k) const { return iterator(avl_.find(k)); }

    T& operator[](const Key& k) {
        typename avltree<Key, T, Compare>::Node* x = avl_.find(k);
        if (x) return x->val;
        return avl_.insert(k, T()).node->val;
    }

    struct R { iterator first; bool second; };
    R insert(const value_type& v) {
        typename avltree<Key, T, Compare>::Ins r = avl_.insert(v.first, v.second);
        return R{ iterator(r.node), r.inserted };
    }

    iterator erase(iterator it) {
        if (!it.n_) return end();
        typename avltree<Key, T, Compare>::Node* nx = avl_.next(it.n_);
        avl_.erase(it.n_->key);
        return iterator(nx);
    }
    size_type erase(const Key& k) { return avl_.erase(k) ? 1 : 0; }

    iterator lower_bound(const Key& k) const { return iterator(avl_.find(k)); }
};

} // namespace stdx
} // namespace nefu
