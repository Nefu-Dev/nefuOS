// nefuOS stdcxx - <iterator>
#pragma once
#include "_core.h"
namespace nefu {
namespace stdx {

struct input_iterator_tag {};
struct output_iterator_tag {};
struct forward_iterator_tag : input_iterator_tag {};
struct bidirectional_iterator_tag : forward_iterator_tag {};
struct random_access_iterator_tag : bidirectional_iterator_tag {};

template <class Container>
auto begin(Container& c) -> decltype(c.begin()) { return c.begin(); }
template <class Container>
auto end(Container& c) -> decltype(c.end()) { return c.end(); }

template <class T, size_t N>
T* begin(T (&a)[N]) { return a; }
template <class T, size_t N>
T* end(T (&a)[N]) { return a + N; }

template <class InputIt, class T>
inline InputIt find(InputIt first, InputIt last, const T& value) {
    for (; first != last; ++first) if (*first == value) return first;
    return last;
}
template <class InputIt, class Pred>
inline InputIt find_if(InputIt first, InputIt last, Pred p) {
    for (; first != last; ++first) if (p(*first)) return first;
    return last;
}

} // namespace stdx
} // namespace nefu
