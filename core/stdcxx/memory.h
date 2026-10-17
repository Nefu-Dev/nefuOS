// nefuOS stdcxx - <memory> (unique_ptr / default_delete)
#pragma once
#include "_core.h"
namespace nefu {
namespace stdx {

template <class T>
struct default_delete {
    void operator()(T* p) const { delete p; }
};
template <class T>
struct default_delete<T[]> {
    void operator()(T* p) const { delete[] p; }
};

template <class T, class D = default_delete<T> >
class unique_ptr {
    T* ptr_;
    D  del_;
public:
    typedef T element_type;
    unique_ptr() : ptr_(0) {}
    explicit unique_ptr(T* p) : ptr_(p) {}
    ~unique_ptr() { reset(); }
    unique_ptr(const unique_ptr&) = delete;
    unique_ptr& operator=(const unique_ptr&) = delete;
    unique_ptr(unique_ptr&& o) : ptr_(o.ptr_) { o.ptr_ = 0; }
    unique_ptr& operator=(unique_ptr&& o) { if (this != &o) { reset(o.ptr_); o.ptr_ = 0; } return *this; }

    void reset(T* p = 0) { if (ptr_) del_(ptr_); ptr_ = p; }
    T* release() { T* p = ptr_; ptr_ = 0; return p; }
    T* get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    T& operator*() const { return *ptr_; }
    explicit operator bool() const { return ptr_ != 0; }
    void swap(unique_ptr& o) { T* t = ptr_; ptr_ = o.ptr_; o.ptr_ = t; }
};

template <class T, class... Args>
unique_ptr<T> make_unique(Args&&... args) { return unique_ptr<T>(new T(forward<Args>(args)...)); }

} // namespace stdx
} // namespace nefu
