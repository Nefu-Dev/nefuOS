// nefuOS stdcxx — internal core (allocator / move / tag dispatch)
// Shared by every C++ standard-library header in this module. Uses the nefuOS
// kernel heap (kalloc/kfree) so containers are uniform with the rest of the OS.
// The whole module is header-only and namespace-isolated as nefu::stdx so it can
// sit beside the real C++ standard library without clashing.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <new>   // placement operator new(size_t, void*) for in-place construction

namespace nefu {
namespace stdx {

typedef decltype(nullptr) nullptr_t;

// --- allocator hook ---
// Host/verification builds use the standard heap. In a real nefuOS kernel
// target, define NEFU_STDX_USE_NEUF_KALLOC and kalloc/kfree (core/klib/klib.h)
// become the backing allocator.
#ifdef NEFU_STDX_USE_NEUF_KALLOC
inline void* allocate(size_t n) { return ::nefu::kalloc(n); }
inline void  deallocate(void* p) { if (p) ::nefu::kfree(p); }
#else
inline void* allocate(size_t n) { return ::operator new(n); }
inline void  deallocate(void* p) { if (p) ::operator delete(p); }
#endif

// ---- move / forward (C++11 subset) ----
template <class T> struct remove_reference      { typedef T type; };
template <class T> struct remove_reference<T&>  { typedef T type; };
template <class T> struct remove_reference<T&&> { typedef T type; };

template <class T>
inline T&& move(T& t) { return static_cast<T&&>(t); }
template <class T>
inline T&& forward(typename remove_reference<T>::type& t) { return static_cast<T&&>(t); }

} // namespace stdx
} // namespace nefu
