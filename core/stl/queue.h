// nefuOS STL - queue implementation
#pragma once

#include "vector.h"

namespace nefu {
namespace stl {

template <typename T>
class queue {
private:
    vector<T> data_;
    
public:
    typedef T value_type;
    typedef size_t size_type;
    
    queue() {}
    
    size_type size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }
    
    void push(const T& value) {
        data_.push_back(value);
    }
    
    void pop() {
        if (!data_.empty()) {
            data_.erase(data_.begin());
        }
    }
    
    T& front() { return data_.front(); }
    const T& front() const { return data_.front(); }
    
    T& back() { return data_.back(); }
    const T& back() const { return data_.back(); }
};

} // namespace stl
} // namespace nefu
