// nefuOS STL - pair
#pragma once

namespace nefu {
namespace stl {

template <typename T1, typename T2>
struct pair {
    typedef T1 first_type;
    typedef T2 second_type;
    
    T1 first;
    T2 second;
    
    pair() : first(), second() {}
    
    pair(const T1& a, const T2& b) : first(a), second(b) {}
    
    template <typename U1, typename U2>
    pair(const pair<U1, U2>& p) : first(p.first), second(p.second) {}
    
    pair& operator=(const pair& other) {
        first = other.first;
        second = other.second;
        return *this;
    }
    
    bool operator==(const pair& other) const {
        return first == other.first && second == other.second;
    }
    
    bool operator!=(const pair& other) const {
        return !(*this == other);
    }
    
    bool operator<(const pair& other) const {
        if (first < other.first) return true;
        if (other.first < first) return false;
        return second < other.second;
    }
    
    bool operator>(const pair& other) const {
        return other < *this;
    }
    
    bool operator<=(const pair& other) const {
        return !(other < *this);
    }
    
    bool operator>=(const pair& other) const {
        return !(*this < other);
    }
};

template <typename T1, typename T2>
pair<T1, T2> make_pair(const T1& a, const T2& b) {
    return pair<T1, T2>(a, b);
}

} // namespace stl
} // namespace nefu
