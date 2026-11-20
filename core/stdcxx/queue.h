/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS stdcxx - <queue> and <stack> (adaptors over list)
#pragma once
#include "_core.h"
#include "list.h"
namespace nefu {
namespace stdx {

template <class T>
class queue {
    list<T> c_;
public:
    queue() {}
    bool empty() const { return c_.empty(); }
    size_t size() const { return c_.size(); }
    T& front() { return c_.front(); }
    T& back()  { return c_.back(); }
    void push(const T& v) { c_.push_back(v); }
    void pop() { c_.pop_front(); }
};

template <class T>
class stack {
    list<T> c_;
public:
    stack() {}
    bool empty() const { return c_.empty(); }
    size_t size() const { return c_.size(); }
    T& top() { return c_.back(); }
    void push(const T& v) { c_.push_back(v); }
    void pop() { c_.pop_back(); }
};

} // namespace stdx
} // namespace nefu

