// nefuOS STL - vector implementation
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace stl {

template <typename T>
class vector {
private:
    T* data_;
    size_t size_;
    size_t capacity_;
    
public:
    typedef T value_type;
    typedef T& reference;
    typedef const T& const_reference;
    typedef T* iterator;
    typedef const T* const_iterator;
    typedef size_t size_type;
    
    vector() : data_(0), size_(0), capacity_(0) {}
    
    vector(size_type n) : data_(0), size_(0), capacity_(0) {
        resize(n);
    }
    
    vector(const vector& other) : data_(0), size_(0), capacity_(0) {
        resize(other.size_);
        for (size_t i = 0; i < size_; i++) {
            data_[i] = other.data_[i];
        }
    }
    
    ~vector() {
        if (data_) kfree(data_);
    }
    
    vector& operator=(const vector& other) {
        if (this == &other) return *this;
        resize(other.size_);
        for (size_t i = 0; i < size_; i++) {
            data_[i] = other.data_[i];
        }
        return *this;
    }
    
    size_type size() const { return size_; }
    bool empty() const { return size_ == 0; }
    size_type capacity() const { return capacity_; }
    
    void reserve(size_type new_cap) {
        if (new_cap <= capacity_) return;
        
        T* new_data = (T*)kalloc(new_cap * sizeof(T));
        if (!new_data) return;
        
        for (size_t i = 0; i < size_; i++) {
            new_data[i] = data_[i];
        }
        
        if (data_) kfree(data_);
        data_ = new_data;
        capacity_ = new_cap;
    }
    
    void resize(size_type n) {
        if (n > capacity_) {
            size_type new_cap = capacity_ * 2;
            if (new_cap < n) new_cap = n;
            reserve(new_cap);
        }
        size_ = n;
    }
    
    void push_back(const T& value) {
        if (size_ >= capacity_) {
            reserve(capacity_ ? capacity_ * 2 : 4);
        }
        data_[size_++] = value;
    }
    
    void pop_back() {
        if (size_ > 0) size_--;
    }
    
    reference operator[](size_type i) { return data_[i]; }
    const_reference operator[](size_type i) const { return data_[i]; }
    
    reference front() { return data_[0]; }
    const_reference front() const { return data_[0]; }
    
    reference back() { return data_[size_ - 1]; }
    const_reference back() const { return data_[size_ - 1]; }
    
    iterator begin() { return data_; }
    const_iterator begin() const { return data_; }
    
    iterator end() { return data_ + size_; }
    const_iterator end() const { return data_ + size_; }
    
    void clear() { size_ = 0; }
    
    iterator insert(iterator pos, const T& value) {
        size_type idx = pos - begin();
        push_back(value);
        for (size_type i = size_ - 1; i > idx; i--) {
            data_[i] = data_[i - 1];
        }
        data_[idx] = value;
        return begin() + idx;
    }
    
    iterator erase(iterator pos) {
        size_type idx = pos - begin();
        for (size_type i = idx; i < size_ - 1; i++) {
            data_[i] = data_[i + 1];
        }
        size_--;
        return begin() + idx;
    }
};

} // namespace stl
} // namespace nefu
