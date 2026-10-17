// nefuOS stdcxx - <vector>
// Dynamic array on the nefuOS kernel heap (kalloc/kfree).
#pragma once
#include "_core.h"
namespace nefu {
namespace stdx {

template <class T>
class vector {
    T*       d_;
    size_t   n_;
    size_t   cap_;

    void grow(size_t need) {
        if (need <= cap_) return;
        size_t ncap = cap_ ? cap_ : 8;
        while (ncap < need) ncap *= 2;
        T* nd = (T*)allocate(ncap * sizeof(T));
        for (size_t i = 0; i < n_; ++i) new (&nd[i]) T(d_[i]);
        for (size_t i = 0; i < n_; ++i) d_[i].~T();
        if (d_) deallocate(d_);
        d_ = nd; cap_ = ncap;
    }

public:
    typedef T value_type;
    typedef size_t size_type;

    vector() : d_(0), n_(0), cap_(0) {}
    explicit vector(size_t n, const T& v = T()) : d_(0), n_(0), cap_(0) { assign(n, v); }
    vector(const vector& o) : d_(0), n_(0), cap_(0) { assign(o.begin(), o.size()); }
    vector& operator=(const vector& o) { if (this != &o) { clear(); assign(o.begin(), o.size()); } return *this; }
    ~vector() { clear(); if (d_) deallocate(d_); }

    template <class It>
    void assign(It first, size_t cnt) {
        clear();
        reserve(cnt);
        for (size_t i = 0; i < cnt; ++i) new (&d_[n_++]) T(*first++);
    }

    void reserve(size_t c) { grow(c); }
    size_t size() const  { return n_; }
    size_t capacity() const { return cap_; }
    bool empty() const   { return n_ == 0; }

    T&       operator[](size_t i)       { return d_[i]; }
    const T& operator[](size_t i) const { return d_[i]; }
    T& at(size_t i)                     { return d_[i]; }
    const T& at(size_t i) const         { return d_[i]; }
    T& front()                          { return d_[0]; }
    T& back()                           { return d_[n_ - 1]; }
    T* data()                           { return d_; }
    const T* data() const               { return d_; }
    T* begin()                          { return d_; }
    T* end()                            { return d_ + n_; }
    const T* begin() const              { return d_; }
    const T* end() const                { return d_ + n_; }

    void push_back(const T& v) { grow(n_ + 1); new (&d_[n_++]) T(v); }
    void pop_back() { if (n_) { d_[--n_].~T(); } }
    void clear() { for (size_t i = 0; i < n_; ++i) d_[i].~T(); n_ = 0; }

    void resize(size_t n, const T& v = T()) {
        if (n > n_) { grow(n); while (n_ < n) new (&d_[n_++]) T(v); }
        else { while (n_ > n) d_[--n_].~T(); }
    }
};

template <class T>
inline bool operator==(const vector<T>& a, const vector<T>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (!(a[i] == b[i])) return false;
    return true;
}

} // namespace stdx
} // namespace nefu
