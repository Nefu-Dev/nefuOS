// nefuOS stdcxx - <array>
#pragma once
#include "_core.h"
namespace nefu {
namespace stdx {

template <class T, size_t N>
class array {
public:
    typedef T value_type;
    typedef size_t size_type;
    T elems[N];

    T&       operator[](size_t i)       { return elems[i]; }
    const T& operator[](size_t i) const { return elems[i]; }
    T&       at(size_t i)               { return elems[i]; }
    const T& at(size_t i) const         { return elems[i]; }
    T&       front()                    { return elems[0]; }
    T&       back()                     { return elems[N - 1]; }
    T*       data()                     { return elems; }
    const T* data() const               { return elems; }
    T*       begin()                    { return elems; }
    T*       end()                      { return elems + N; }
    const T* begin() const              { return elems; }
    const T* end() const                { return elems + N; }
    static size_type size()             { return N; }
    static size_type max_size()         { return N; }
    bool empty() const                  { return N == 0; }
    void fill(const T& v)               { for (size_t i = 0; i < N; ++i) elems[i] = v; }
};

template <class T, size_t N>
inline bool operator==(const array<T, N>& a, const array<T, N>& b) {
    for (size_t i = 0; i < N; ++i) if (!(a[i] == b[i])) return false;
    return true;
}
template <class T, size_t N>
inline bool operator!=(const array<T, N>& a, const array<T, N>& b) { return !(a == b); }

} // namespace stdx
} // namespace nefu
