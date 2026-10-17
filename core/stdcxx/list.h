// nefuOS stdcxx - <list>
// Doubly-linked list on the nefuOS kernel heap.
#pragma once
#include "_core.h"
namespace nefu {
namespace stdx {

template <class T>
class list {
    struct Node { T val; Node* prev; Node* next; };
    Node* head_;  // sentinel
    size_t n_;

    Node* new_node(const T& v) {
        Node* nd = (Node*)allocate(sizeof(Node));
        nd->val = v; nd->prev = 0; nd->next = 0;
        return nd;
    }
    void link_before(Node* pos, Node* nd) { nd->prev = pos->prev; nd->next = pos; pos->prev->next = nd; pos->prev = nd; }
    void unlink(Node* nd) { nd->prev->next = nd->next; nd->next->prev = nd->prev; nd->prev = nd->next = 0; }

public:
    class iterator {
    public:
        Node* p_;
        iterator() : p_(0) {}
        iterator(Node* p) : p_(p) {}
        T& operator*() const { return p_->val; }
        T* operator->() const { return &p_->val; }
        iterator& operator++() { p_ = p_->next; return *this; }
        iterator& operator--() { p_ = p_->prev; return *this; }
        bool operator==(const iterator& o) const { return p_ == o.p_; }
        bool operator!=(const iterator& o) const { return p_ != o.p_; }
    };

    list() : n_(0) { head_ = new_node(T()); head_->prev = head_; head_->next = head_; }
    list(const list& o) : n_(0) { head_ = new_node(T()); head_->prev = head_; head_->next = head_; for (iterator it = o.begin(); it != o.end(); ++it) push_back(*it); }
    list& operator=(const list& o) { if (this != &o) { clear(); for (iterator it = o.begin(); it != o.end(); ++it) push_back(*it); } return *this; }
    ~list() { clear(); deallocate(head_); }

    iterator begin() { return iterator(head_->next); }
    iterator end()   { return iterator(head_); }
    size_t size() const { return n_; }
    bool empty() const { return n_ == 0; }

    T& front() { return head_->next->val; }
    T& back()  { return head_->prev->val; }

    void push_back(const T& v)  { Node* nd = new_node(v); link_before(head_, nd); ++n_; }
    void push_front(const T& v) { Node* nd = new_node(v); link_before(head_->next, nd); ++n_; }
    void pop_back()  { if (n_) { Node* nd = head_->prev; unlink(nd); deallocate(nd); --n_; } }
    void pop_front() { if (n_) { Node* nd = head_->next; unlink(nd); deallocate(nd); --n_; } }
    void clear() { while (n_) pop_front(); }

    iterator insert(iterator pos, const T& v) { Node* nd = new_node(v); link_before(pos.p_, nd); ++n_; return iterator(nd); }
    iterator erase(iterator pos) { if (pos.p_ == head_) return pos; Node* nd = pos.p_; Node* nx = nd->next; unlink(nd); deallocate(nd); --n_; return iterator(nx); }
};

} // namespace stdx
} // namespace nefu
