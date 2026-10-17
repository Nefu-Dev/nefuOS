// nefuOS stdcxx - <functional> (comparison functors + basic hash)
#pragma once
#include "_core.h"
namespace nefu {
namespace stdx {

template <class T> struct less      { bool operator()(const T& a, const T& b) const { return a < b; } };
template <class T> struct greater   { bool operator()(const T& a, const T& b) const { return a > b; } };
template <class T> struct less_equal{ bool operator()(const T& a, const T& b) const { return a <= b; } };
template <class T> struct greater_equal { bool operator()(const T& a, const T& b) const { return a >= b; } };
template <class T> struct equal_to  { bool operator()(const T& a, const T& b) const { return a == b; } };
template <class T> struct not_equal_to { bool operator()(const T& a, const T& b) const { return a != b; } };
template <class T> struct plus      { T operator()(const T& a, const T& b) const { return a + b; } };
template <class T> struct minus     { T operator()(const T& a, const T& b) const { return a - b; } };

template <class T> struct hash {
    size_t operator()(const T& v) const { return (size_t)v; }
};

} // namespace stdx
} // namespace nefu
