/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS STL - stack implementation
#pragma once

#include "vector.h"

namespace nefu {
namespace stl {

template <typename T>
class stack {
private:
    vector<T> data_;
    
public:
    typedef T value_type;
    typedef size_t size_type;
    
    stack() {}
    
    size_type size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }
    
    void push(const T& value) {
        data_.push_back(value);
    }
    
    void pop() {
        if (!data_.empty()) {
            data_.pop_back();
        }
    }
    
    T& top() { return data_.back(); }
    const T& top() const { return data_.back(); }
};

} // namespace stl
} // namespace nefu

