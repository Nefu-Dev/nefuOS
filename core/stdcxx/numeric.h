// nefuOS stdcxx - <numeric>
#pragma once
#include "_core.h"
namespace nefu {
namespace stdx {

template <class It, class T>
inline T accumulate(It first, It last, T init) { for (; first != last; ++first) init = init + *first; return init; }
template <class It, class T, class BinOp>
inline T accumulate(It first, It last, T init, BinOp op) { for (; first != last; ++first) init = op(init, *first); return init; }

template <class It, class T>
inline void iota(It first, It last, T value) { for (; first != last; ++first, ++value) *first = value; }

} // namespace stdx
} // namespace nefu
