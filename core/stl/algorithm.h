// nefuOS STL - algorithms
#pragma once

namespace nefu {
namespace stl {

template <typename T>
void swap(T& a, T& b) {
    T tmp = a;
    a = b;
    b = tmp;
}

template <typename Iterator, typename T>
Iterator find(Iterator first, Iterator last, const T& value) {
    while (first != last) {
        if (*first == value) return first;
        ++first;
    }
    return last;
}

template <typename Iterator, typename Predicate>
Iterator find_if(Iterator first, Iterator last, Predicate pred) {
    while (first != last) {
        if (pred(*first)) return first;
        ++first;
    }
    return last;
}

template <typename Iterator>
void reverse(Iterator first, Iterator last) {
    while (first != last && first != --last) {
        swap(*first, *last);
        ++first;
    }
}

template <typename Iterator>
void sort(Iterator first, Iterator last) {
    // Simple bubble sort
    for (Iterator i = first; i != last; ++i) {
        for (Iterator j = first; j != last - 1; ++j) {
            if (*(j + 1) < *j) {
                swap(*j, *(j + 1));
            }
        }
    }
}

template <typename Iterator, typename Compare>
void sort(Iterator first, Iterator last, Compare comp) {
    for (Iterator i = first; i != last; ++i) {
        for (Iterator j = first; j != last - 1; ++j) {
            if (comp(*(j + 1), *j)) {
                swap(*j, *(j + 1));
            }
        }
    }
}

template <typename Iterator, typename T>
size_t count(Iterator first, Iterator last, const T& value) {
    size_t result = 0;
    while (first != last) {
        if (*first == value) result++;
        ++first;
    }
    return result;
}

template <typename Iterator, typename Predicate>
size_t count_if(Iterator first, Iterator last, Predicate pred) {
    size_t result = 0;
    while (first != last) {
        if (pred(*first)) result++;
        ++first;
    }
    return result;
}

template <typename Iterator, typename T>
void fill(Iterator first, Iterator last, const T& value) {
    while (first != last) {
        *first = value;
        ++first;
    }
}

template <typename Iterator>
Iterator min_element(Iterator first, Iterator last) {
    if (first == last) return last;
    Iterator result = first;
    while (++first != last) {
        if (*first < *result) result = first;
    }
    return result;
}

template <typename Iterator>
Iterator max_element(Iterator first, Iterator last) {
    if (first == last) return last;
    Iterator result = first;
    while (++first != last) {
        if (*result < *first) result = first;
    }
    return result;
}

template <typename T>
const T& min(const T& a, const T& b) {
    return (a < b) ? a : b;
}

template <typename T>
const T& max(const T& a, const T& b) {
    return (a < b) ? b : a;
}

template <typename Iterator>
bool equal(Iterator first1, Iterator last1, Iterator first2) {
    while (first1 != last1) {
        if (*first1 != *first2) return false;
        ++first1;
        ++first2;
    }
    return true;
}

template <typename Iterator1, typename Iterator2>
Iterator1 search(Iterator1 first1, Iterator1 last1, Iterator2 first2, Iterator2 last2) {
    if (first2 == last2) return first1;
    while (first1 != last1) {
        Iterator1 it1 = first1;
        Iterator2 it2 = first2;
        while (it1 != last1 && it2 != last2 && *it1 == *it2) {
            ++it1;
            ++it2;
        }
        if (it2 == last2) return first1;
        ++first1;
    }
    return last1;
}

} // namespace stl
} // namespace nefu
