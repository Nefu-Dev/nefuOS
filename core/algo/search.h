// nefuOS search algorithm library
// (1) classic array searches: linear, binary, ternary, interpolation,
//     exponential — with documented complexity and integer semantics;
// (2) string matching: naive, Knuth-Morris-Pratt, Rabin-Karp — all
//     operating on plain char arrays, no STL.
// Written for study: each function carries step comments and the self test
// at the bottom validates every algorithm against known cases.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace algo {

// =====================================================================
// array search (returns index or -1; lo/hi are inclusive bounds)
// =====================================================================

// Linear search: the baseline.  O(n) in all cases.
int search_linear(const int* a, int n, int key);

// Binary search on a sorted (ascending) array.  O(log n).
// Returns the index of an exact match, or -(insertion_point+1) so the
// caller can recover the first index >= key from the return value.
int search_binary(const int* a, int n, int key);

// Ternary search on a sorted array: two pivots, three-way split.
// Still O(log n) but with a worse constant than binary; included as the
// classic divide-and-conquer variant.
int search_ternary(const int* a, int lo, int hi, int key);

// Interpolation search: binary search but guessing the probe position by
// linear interpolation between a[lo] and a[hi].  Best O(log log n) on
// uniformly distributed keys, worst O(n).
int search_interp(const int* a, int n, int key);

// Exponential search: probes a[0], a[1], a[2], a[4], ... then binary
// searches the found window.  O(log n); excellent for unbounded arrays.
int search_exp(const int* a, int n, int key);

// =====================================================================
// string matching (returns first index in haystack, or -1)
// =====================================================================

// Naive / brute force: try every alignment, compare char by char.
//   best O(n+m)  worst O(n*m)
int strfind_naive(const char* text, int n, const char* pat, int m);

// Knuth-Morris-Pratt: builds the failure (pi) table so text characters
// are never revisited.  O(n + m) in all cases.
int strfind_kmp(const char* text, int n, const char* pat, int m);

// Rabin-Karp: rolling hash comparison.  O(n + m) expected.
int strfind_rk(const char* text, int n, const char* pat, int m);

// =====================================================================
// self tests
// =====================================================================
int search_self_test();

} // namespace algo
} // namespace nefu
