// nefuOS stdcxx — <utility>
#pragma once
#include "_core.h"
namespace nefu {
namespace stdx {

// pair
template <class T1, class T2>
struct pair {
    typedef T1 first_type;
    typedef T2 second_type;
    T1 first;
    T2 second;
    pair() : first(), second() {}
    pair(const T1& a, const T2& b) : first(a), second(b) {}
    template <class U1, class U2>
    pair(const pair<U1, U2>& o) : first(o.first), second(o.second) {}
    template <class U1, class U2>
    pair& operator=(const pair<U1, U2>& o) { first = o.first; second = o.second; return *this; }
};

template <class T1, class T2>
inline bool operator==(const pair<T1, T2>& a, const pair<T1, T2>& b) {
    return a.first == b.first && a.second == b.second;
}
template <class T1, class T2>
inline bool operator!=(const pair<T1, T2>& a, const pair<T1, T2>& b) { return !(a == b); }
template <class T1, class T2>
inline bool operator<(const pair<T1, T2>& a, const pair<T1, T2>& b) {
    return a.first < b.first || (!(b.first < a.first) && a.second < b.second);
}

template <class T1, class T2>
inline pair<T1, T2> make_pair(T1 a, T2 b) { return pair<T1, T2>(a, b); }

// swap
template <class T>
inline void swap(T& a, T& b) { T t = a; a = b; b = t; }

// exchange / clamp helpers
template <class T, class U>
inline T exchange(T& obj, U&& new_value) { T old = obj; obj = new_value; return old; }

} // namespace stdx
} // namespace nefu
