// nefuOS sorting algorithm library
// A complete, teaching-oriented collection of comparison sorts plus the
// classic non-comparison sorts, all written from scratch with no STL.
// Every algorithm documents its worst/average/best complexity and whether
// it is stable, so the library doubles as study material.
// All functions operate in place on int arrays; a comparator may be
// supplied (cmp_asc default).  Comparisons follow the C qsort convention:
// return -1/0/1 for a<b / a==b / a>b.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace algo {

// ---- comparator ----
typedef int (*CompareFn)(int a, int b);
int cmp_asc(int a, int b);     // -1, 0, 1  (ascending)
int cmp_desc(int a, int b);    //  1, 0, -1 (descending)

// ---- in-place helper ----
inline void swap_int(int& a, int& b) { int t = a; a = b; b = t; }

// ---- O(n^2) comparison sorts ----
// Bubble sort: stable.  Nested passes; each pass floats the largest element
// (ascending) to the end.  Early exit when a pass makes no swap.
//   best O(n)  average O(n^2)  worst O(n^2)  space O(1)
void sort_bubble(int* a, int n, CompareFn cmp = 0);

// Selection sort: unstable.  For each position pick the smallest of the
// remaining suffix and swap it into place.  Minimum number of swaps (n-1).
//   all cases O(n^2)  space O(1)
void sort_selection(int* a, int n, CompareFn cmp = 0);

// Insertion sort: stable.  Grow a sorted prefix by inserting each new
// element into its place, shifting the rest right.  Best for nearly-sorted
// data and small n.
//   best O(n)  average O(n^2)  worst O(n^2)  space O(1)
void sort_insertion(int* a, int n, CompareFn cmp = 0);

// Gnome sort (stubborn sort): stable.  A walking insertion sort: move
// forward while in order, otherwise swap backward until back in order.
//   best O(n)  average O(n^2)  worst O(n^2)  space O(1)
void sort_gnome(int* a, int n, CompareFn cmp = 0);

// Shell sort: unstable.  Insertion sort over shrinking gaps; the gap
// sequence used here is the classic Hibbard-style halving from n/2 down.
//   best O(n log n)  average O(n^1.5)  worst O(n^2)  space O(1)
void sort_shell(int* a, int n, CompareFn cmp = 0);

// ---- O(n log n) comparison sorts ----
// Merge sort: stable, top-down recursive with an explicit scratch buffer
// allocated by the caller (avoids malloc per call).  Splits at the middle.
//   all cases O(n log n)  space O(n)
void sort_merge(int* a, int n, int* scratch, CompareFn cmp = 0);
void sort_merge(int* a, int n, CompareFn cmp = 0);          // internal scratch version

// Quick sort: unstable.  Median-of-three pivot, in-place partition, and a
// tail-recursion guard so the worst-case stack depth stays O(log n).
//   best O(n log n)  average O(n log n)  worst O(n^2)  space O(log n)
void sort_quick(int* a, int n, CompareFn cmp = 0);

// Heap sort: unstable.  Build a max-heap then repeatedly extract the max
// into the tail.  In-place with O(1) extra space.
//   all cases O(n log n)  space O(1)
void sort_heap(int* a, int n, CompareFn cmp = 0);

// ---- non-comparison sorts (keys must be integers in a known range) ----
// Counting sort: stable.  Needs the key range [lo, hi]; three passes
// (count, prefix-sum, place from the back for stability).
//   all cases O(n + range)  space O(n + range)
void sort_counting(int* a, int n, int lo, int hi);

// Radix sort (LSD): stable.  Sorts digit by digit with counting sort;
// here the base is 256 and keys are non-negative (range normalized).
//   all cases O(d * (n + base))  space O(n + base)
void sort_radix(int* a, int n);

// Bucket sort: stable.  Distributes into `nb` buckets over [lo, hi] and
// insertion-sorts each bucket.  Good when keys are uniformly spread.
//   best O(n + nb)  average O(n)  worst O(n^2)  space O(n + nb)
void sort_bucket(int* a, int n, int lo, int hi, int nb = 8);

// ---- verification helpers ----
// true when a[0..n-1] is sorted under cmp (cmp=0 -> ascending)
bool is_sorted(const int* a, int n, CompareFn cmp = 0);
// true when b is a permutation of a (multiset equality), both length n
bool is_permutation(const int* a, const int* b, int n);

// ---- self test ----
// Runs every sort on random / sorted / reverse-sorted / duplicate /
// small and one-element arrays and asserts output correctness.
// Returns the number of failed assertions (0 == all green).
int sort_self_test();

} // namespace algo
} // namespace nefu
