// nefuOS stdcxx - <algorithm>
#pragma once
#include "_core.h"
#include "functional.h"
namespace nefu {
namespace stdx {

template <class T>
inline const T& min(const T& a, const T& b) { return b < a ? b : a; }
template <class T, class Compare>
inline const T& min(const T& a, const T& b, Compare cmp) { return cmp(b, a) ? b : a; }
template <class T>
inline const T& max(const T& a, const T& b) { return a < b ? b : a; }
template <class T, class Compare>
inline const T& max(const T& a, const T& b, Compare cmp) { return cmp(a, b) ? b : a; }
template <class T>
inline const T& clamp(const T& v, const T& lo, const T& hi) { return v < lo ? lo : (hi < v ? hi : v); }

template <class It, class T>
inline size_t count(It first, It last, const T& value) { size_t n = 0; for (; first != last; ++first) if (*first == value) ++n; return n; }

template <class It>
inline void reverse(It first, It last) { while (first != last) { --last; if (first == last) break; swap(*first, *last); ++first; } }

template <class It>
inline It max_element(It first, It last) {
    if (first == last) return last;
    It best = first;
    for (++first; first != last; ++first) if (*best < *first) best = first;
    return best;
}
template <class It>
inline It min_element(It first, It last) {
    if (first == last) return last;
    It best = first;
    for (++first; first != last; ++first) if (*first < *best) best = first;
    return best;
}

// heap sort: O(n log n), no recursion, works with random-access iterators.
template <class It, class Compare>
inline void sort(It first, It last, Compare cmp) {
    ptrdiff_t n = last - first;
    if (n < 2) return;
    // build max-heap
    for (ptrdiff_t i = n / 2 - 1; i >= 0; --i) {
        ptrdiff_t p = i;
        for (;;) {
            ptrdiff_t c = p * 2 + 1; if (c >= n) break;
            if (c + 1 < n && cmp(first[c], first[c + 1])) ++c;
            if (!cmp(first[p], first[c])) break;
            swap(first[p], first[c]); p = c;
        }
    }
    for (ptrdiff_t i = n - 1; i > 0; --i) {
        swap(first[0], first[i]);
        ptrdiff_t p = 0, sz = i;
        for (;;) {
            ptrdiff_t c = p * 2 + 1; if (c >= sz) break;
            if (c + 1 < sz && cmp(first[c], first[c + 1])) ++c;
            if (!cmp(first[p], first[c])) break;
            swap(first[p], first[c]); p = c;
        }
    }
}
template <class It>
inline void sort(It first, It last) { sort(first, last, less<decltype(*first)>()); }

} // namespace stdx
} // namespace nefu
