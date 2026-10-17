// nefuOS stdcxx — <type_traits>
#pragma once
#include "_core.h"
namespace nefu {
namespace stdx {

template <class T, T v>
struct integral_constant {
    static const T value = v;
    typedef T value_type;
    typedef integral_constant<T, v> type;
    operator T() const { return v; }
};
typedef integral_constant<bool, true>  true_type;
typedef integral_constant<bool, false> false_type;

// remove / decay
template <class T> struct remove_const          { typedef T type; };
template <class T> struct remove_const<const T> { typedef T type; };
template <class T> struct remove_volatile             { typedef T type; };
template <class T> struct remove_volatile<volatile T> { typedef T type; };
template <class T> struct remove_cv { typedef typename remove_volatile<typename remove_const<T>::type>::type type; };
template <class T> struct remove_pointer          { typedef T type; };
template <class T> struct remove_pointer<T*>      { typedef T type; };
template <class T> struct remove_pointer<T* const>{ typedef T type; };
template <class T> struct decay { typedef typename remove_cv<T>::type type; };
template <class T> struct decay<T*> { typedef T* type; };

// is_same / is_void / is_integral / is_floating_point / is_arithmetic / is_pointer
template <class T, class U> struct is_same : false_type {};
template <class T> struct is_same<T, T> : true_type {};

template <class T> struct is_void : is_same<typename remove_cv<T>::type, void> {};

template <class T> struct is_integral      : false_type {};
template <> struct is_integral<bool>               : true_type {};
template <> struct is_integral<char>               : true_type {};
template <> struct is_integral<signed char>        : true_type {};
template <> struct is_integral<unsigned char>      : true_type {};
template <> struct is_integral<short>              : true_type {};
template <> struct is_integral<unsigned short>     : true_type {};
template <> struct is_integral<int>                : true_type {};
template <> struct is_integral<unsigned int>       : true_type {};
template <> struct is_integral<long>               : true_type {};
template <> struct is_integral<unsigned long>      : true_type {};
template <> struct is_integral<long long>          : true_type {};
template <> struct is_integral<unsigned long long> : true_type {};

template <class T> struct is_floating_point : false_type {};
template <> struct is_floating_point<float>  : true_type {};
template <> struct is_floating_point<double> : true_type {};
template <> struct is_floating_point<long double> : true_type {};

template <class T> struct is_arithmetic : integral_constant<bool, is_integral<T>::value || is_floating_point<T>::value> {};
template <class T> struct is_pointer    : false_type {};
template <class T> struct is_pointer<T*> : true_type {};

template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
template <bool B, class T, class F> struct conditional { typedef F type; };
template <class T, class F> struct conditional<true, T, F> { typedef T type; };

} // namespace stdx
} // namespace nefu
