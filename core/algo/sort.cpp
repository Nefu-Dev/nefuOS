// nefuOS sorting algorithm library — implementation & self test
// See sort.h for the API contract and complexity documentation.
// All sorts are written with explicit step comments for study; the self
// test at the bottom runs every algorithm against several input shapes.
#include "sort.h"
#include <string.h>

namespace nefu {
namespace algo {

// ---------------------------------------------------------------------
// comparators
// ---------------------------------------------------------------------
int cmp_asc(int a, int b)  { return a < b ? -1 : (a > b ? 1 : 0); }
int cmp_desc(int a, int b) { return a > b ? -1 : (a < b ? 1 : 0); }

static inline int do_cmp(int a, int b, CompareFn cmp) {
    return cmp ? cmp(a, b) : cmp_asc(a, b);
}

// ---------------------------------------------------------------------
// bubble sort — stable, O(n^2)
// ---------------------------------------------------------------------
void sort_bubble(int* a, int n, CompareFn cmp) {
    if (n <= 1) return;
    bool swapped = true;
    // after k passes the last k elements are in final position
    for (int i = 0; i < n - 1 && swapped; i++) {
        swapped = false;
        for (int j = 0; j < n - 1 - i; j++) {
            if (do_cmp(a[j], a[j + 1], cmp) > 0) {
                swap_int(a[j], a[j + 1]);
                swapped = true;
            }
        }
    }
}

// ---------------------------------------------------------------------
// selection sort — unstable, O(n^2), minimal swaps
// ---------------------------------------------------------------------
void sort_selection(int* a, int n, CompareFn cmp) {
    for (int i = 0; i < n - 1; i++) {
        int best = i;
        for (int j = i + 1; j < n; j++)
            if (do_cmp(a[j], a[best], cmp) < 0) best = j;
        if (best != i) swap_int(a[i], a[best]);
    }
}

// ---------------------------------------------------------------------
// insertion sort — stable, O(n^2), best O(n)
// ---------------------------------------------------------------------
void sort_insertion(int* a, int n, CompareFn cmp) {
    for (int i = 1; i < n; i++) {
        int key = a[i];
        int j = i - 1;
        // shift the sorted prefix right until the key's place opens up
        while (j >= 0 && do_cmp(a[j], key, cmp) > 0) {
            a[j + 1] = a[j];
            j--;
        }
        a[j + 1] = key;
    }
}

// ---------------------------------------------------------------------
// gnome sort — stable, walking insertion sort
// ---------------------------------------------------------------------
void sort_gnome(int* a, int n, CompareFn cmp) {
    int i = 0;
    while (i < n) {
        if (i == 0 || do_cmp(a[i - 1], a[i], cmp) <= 0) {
            i++;                       // prefix is fine, walk forward
        } else {
            swap_int(a[i - 1], a[i]);  // bubble the offender backward
            i--;
        }
    }
}

// ---------------------------------------------------------------------
// shell sort — unstable, gap sequence halves from n/2
// ---------------------------------------------------------------------
void sort_shell(int* a, int n, CompareFn cmp) {
    for (int gap = n / 2; gap > 0; gap /= 2) {
        // insertion sort on the sub-sequence a[gap..n) spaced by `gap`
        for (int i = gap; i < n; i++) {
            int key = a[i];
            int j = i;
            while (j >= gap && do_cmp(a[j - gap], key, cmp) > 0) {
                a[j] = a[j - gap];
                j -= gap;
            }
            a[j] = key;
        }
    }
}

// ---------------------------------------------------------------------
// merge sort — stable, O(n log n), needs O(n) scratch
// ---------------------------------------------------------------------
static void merge_into(int* a, int lo, int mid, int hi, int* scratch, CompareFn cmp) {
    // merge two sorted halves a[lo..mid) and a[mid..hi) through scratch
    int i = lo, j = mid, k = lo;
    while (i < mid && j < hi)
        scratch[k++] = (do_cmp(a[i], a[j], cmp) <= 0) ? a[i++] : a[j++];
    while (i < mid) scratch[k++] = a[i++];
    while (j < hi)  scratch[k++] = a[j++];
    for (k = lo; k < hi; k++) a[k] = scratch[k];
}

static void merge_rec(int* a, int lo, int hi, int* scratch, CompareFn cmp) {
    if (hi - lo < 2) return;               // 0 or 1 element: already sorted
    int mid = lo + (hi - lo) / 2;
    merge_rec(a, lo, mid, scratch, cmp);
    merge_rec(a, mid, hi, scratch, cmp);
    merge_into(a, lo, mid, hi, scratch, cmp);
}

void sort_merge(int* a, int n, int* scratch, CompareFn cmp) {
    if (n <= 1 || !scratch) return;
    merge_rec(a, 0, n, scratch, cmp);
}

// caller-friendly wrapper: uses a fixed 4 KiB stack buffer for small arrays
void sort_merge(int* a, int n, CompareFn cmp) {
    if (n <= 1024) {
        int scratch[1024];
        sort_merge(a, n, scratch, cmp);
    } else {
        // fall back to an iterative bottom-up merge with dynamic scratch
        int* tmp = new int[(size_t)n];
        if (!tmp) return;
        // bottom-up: width doubles each round
        for (int w = 1; w < n; w *= 2) {
            for (int lo = 0; lo < n; lo += 2 * w) {
                int mid = lo + w; if (mid > n) mid = n;
                int hi  = lo + 2 * w; if (hi > n) hi = n;
                merge_into(a, lo, mid, hi, tmp, cmp);
            }
        }
        delete[] tmp;
    }
}

// ---------------------------------------------------------------------
// quick sort — unstable, median-of-three + tail recursion guard
// ---------------------------------------------------------------------
static int partition(int* a, int lo, int hi, CompareFn cmp) {
    // median-of-three: a[lo], a[mid], a[hi-1] -> pivot at lo
    int mid = lo + (hi - lo) / 2;
    if (do_cmp(a[mid], a[lo], cmp) < 0) swap_int(a[mid], a[lo]);
    if (do_cmp(a[hi - 1], a[lo], cmp) < 0) swap_int(a[hi - 1], a[lo]);
    if (do_cmp(a[hi - 1], a[mid], cmp) < 0) swap_int(a[hi - 1], a[mid]);
    swap_int(a[lo], a[mid]);   // pivot now at a[lo]
    int pivot = a[lo];
    int i = lo + 1, j = hi - 1;
    // classic Hoare-style two-pointer scan with <= pivot on the left
    for (;;) {
        while (i < hi && do_cmp(a[i], pivot, cmp) <= 0) i++;
        while (j > lo && do_cmp(a[j], pivot, cmp) > 0) j--;
        if (i >= j) break;
        swap_int(a[i], a[j]);
        i++; j--;
    }
    swap_int(a[lo], a[j]);     // put the pivot into its final place
    return j;                  // final pivot position
}

static void quick_rec(int* a, int lo, int hi, CompareFn cmp) {
    while (hi - lo > 1) {
        int p = partition(a, lo, hi, cmp);
        // recurse on the smaller side, loop on the larger: O(log n) stack
        if (p - lo < hi - (p + 1)) {
            quick_rec(a, lo, p, cmp);
            lo = p + 1;
        } else {
            quick_rec(a, p + 1, hi, cmp);
            hi = p;
        }
    }
}

void sort_quick(int* a, int n, CompareFn cmp) {
    if (n <= 1) return;
    quick_rec(a, 0, n, cmp);
}

// ---------------------------------------------------------------------
// heap sort — unstable, O(n log n), in-place
// ---------------------------------------------------------------------
static void heap_sift_down(int* a, int n, int i, CompareFn cmp) {
    // max-heap: larger child wins; sift a[i] down to its level
    for (;;) {
        int l = 2 * i + 1, r = 2 * i + 2, m = i;
        if (l < n && do_cmp(a[l], a[m], cmp) > 0) m = l;
        if (r < n && do_cmp(a[r], a[m], cmp) > 0) m = r;
        if (m == i) break;
        swap_int(a[i], a[m]);
        i = m;
    }
}

void sort_heap(int* a, int n, CompareFn cmp) {
    if (n <= 1) return;
    // phase 1: heapify bottom-up (sift from n/2-1 down to 0)
    for (int i = n / 2 - 1; i >= 0; i--) heap_sift_down(a, n, i, cmp);
    // phase 2: repeatedly move the max to the end, shrink the heap
    for (int end = n - 1; end > 0; end--) {
        swap_int(a[0], a[end]);
        heap_sift_down(a, end, 0, cmp);
    }
}

// ---------------------------------------------------------------------
// counting sort — stable, O(n + range)
// ---------------------------------------------------------------------
void sort_counting(int* a, int n, int lo, int hi) {
    if (n <= 1) return;
    int range = hi - lo + 1;
    if (range <= 0 || range > 1 << 20) return;   // sanity bound
    int* count = new int[(size_t)range];
    int* out = new int[(size_t)n];
    if (!count || !out) { delete[] count; delete[] out; return; }
    for (int i = 0; i < range; i++) count[i] = 0;
    // pass 1: frequency of each key
    for (int i = 0; i < n; i++) count[a[i] - lo]++;
    // pass 2: prefix sums -> end position of each key
    int sum = 0;
    for (int i = 0; i < range; i++) { int c = count[i]; count[i] = sum; sum += c; }
    // pass 3: place from the back so equal keys keep input order (stable)
    for (int i = 0; i < n; i++) {
        int k = a[i] - lo;
        out[count[k]++] = a[i];
    }
    for (int i = 0; i < n; i++) a[i] = out[i];
    delete[] count;
    delete[] out;
}

// ---------------------------------------------------------------------
// radix sort — LSD, base 256, non-negative keys normalized to >= 0
// ---------------------------------------------------------------------
void sort_radix(int* a, int n) {
    if (n <= 1) return;
    // normalize: shift the range so the minimum becomes 0
    int mn = a[0], mx = a[0];
    for (int i = 1; i < n; i++) {
        if (a[i] < mn) mn = a[i];
        if (a[i] > mx) mx = a[i];
    }
    int* tmp = new int[(size_t)n];
    if (!tmp) return;
    // 4 passes for 32-bit keys (8 bits each)
    int passes = 4;
    int* src = a;
    int* dst = tmp;
    for (int p = 0; p < passes; p++) {
        int count[256];
        for (int i = 0; i < 256; i++) count[i] = 0;
        int shift = p * 8;
        for (int i = 0; i < n; i++) {
            uint32_t v = (uint32_t)(src[i] - mn);   // normalized, >= 0
            count[(v >> shift) & 0xFF]++;
        }
        int sum = 0;
        for (int i = 0; i < 256; i++) { int c = count[i]; count[i] = sum; sum += c; }
        for (int i = 0; i < n; i++) {
            uint32_t v = (uint32_t)(src[i] - mn);
            dst[count[(v >> shift) & 0xFF]++] = src[i];
        }
        int* t = src; src = dst; dst = t;
    }
    if (src != a) memcpy(a, src, (size_t)n * sizeof(int));
    delete[] tmp;
}

// ---------------------------------------------------------------------
// bucket sort — stable, spreads [lo,hi] into nb buckets, insertion per bucket
// ---------------------------------------------------------------------
void sort_bucket(int* a, int n, int lo, int hi, int nb) {
    if (n <= 1) return;
    int range = hi - lo + 1;
    if (range <= 0) return;
    if (nb < 1) nb = 1;
    if (nb > 256) nb = 256;
    int* buckets = new int[(size_t)nb * (size_t)n];   // flat bucket storage
    int* counts = new int[(size_t)nb];
    if (!buckets || !counts) { delete[] buckets; delete[] counts; return; }
    for (int i = 0; i < nb; i++) counts[i] = 0;
    // distribute
    for (int i = 0; i < n; i++) {
        int idx = (int)(((int64_t)(a[i] - lo) * nb) / range);
        if (idx < 0) idx = 0;
        if (idx >= nb) idx = nb - 1;
        buckets[(size_t)idx * (size_t)n + counts[idx]++] = a[i];
    }
    // sort each bucket (insertion sort: small, stable) and refill
    int out = 0;
    for (int b = 0; b < nb; b++) {
        int* seg = buckets + (size_t)b * (size_t)n;
        int m = counts[b];
        for (int i = 1; i < m; i++) {
            int key = seg[i];
            int j = i - 1;
            while (j >= 0 && seg[j] > key) { seg[j + 1] = seg[j]; j--; }
            seg[j + 1] = key;
        }
        for (int i = 0; i < m; i++) a[out++] = seg[i];
    }
    delete[] buckets;
    delete[] counts;
}

// ---------------------------------------------------------------------
// verification helpers
// ---------------------------------------------------------------------
bool is_sorted(const int* a, int n, CompareFn cmp) {
    for (int i = 1; i < n; i++)
        if (do_cmp(a[i - 1], a[i], cmp) > 0) return false;
    return true;
}

bool is_permutation(const int* a, const int* b, int n) {
    // multiset equality: both arrays must contain the same values.
    // cheap histogram approach with a sentinel-free scan: copy a, delete
    // every value found in b.
    int* copy = new int[(size_t)n];
    if (!copy) return false;
    for (int i = 0; i < n; i++) copy[i] = a[i];
    bool ok = true;
    for (int i = 0; i < n && ok; i++) {
        int j;
        for (j = 0; j < n; j++) {
            if (copy[j] == b[i]) { copy[j] = 0x40000000; break; }  // consume
        }
        if (j == n) ok = false;
    }
    delete[] copy;
    return ok;
}

// ---------------------------------------------------------------------
// self test
// ---------------------------------------------------------------------
namespace {
int g_sort_fails = 0;

void check_sort(const char* name, void (*fn)(int*, int, CompareFn), int* a, int n, CompareFn cmp) {
    int* copy = new int[(size_t)n];
    for (int i = 0; i < n; i++) copy[i] = a[i];
    fn(copy, n, cmp);
    if (!is_sorted(copy, n, cmp) || !is_permutation(a, copy, n)) {
        g_sort_fails++;
        // print the first failing case compactly via a local buffer
    }
    delete[] copy;
}

void check_sort_cmp(const char* name, void (*fn)(int*, int, CompareFn), int* a, int n) {
    // run both directions with two comparators
    check_sort(name, fn, a, n, cmp_asc);
    check_sort(name, fn, a, n, cmp_desc);
}

void check_noncmp(const char* name, void (*fn)(int*, int, int, int), int* a, int n, int lo, int hi) {
    int* copy = new int[(size_t)n];
    for (int i = 0; i < n; i++) copy[i] = a[i];
    fn(copy, n, lo, hi);
    if (!is_sorted(copy, n, 0) || !is_permutation(a, copy, n)) g_sort_fails++;
    delete[] copy;
}

void check_noncmp5(const char* name, void (*fn)(int*, int, int, int, int), int* a, int n, int lo, int hi, int nb) {
    int* copy = new int[(size_t)n];
    for (int i = 0; i < n; i++) copy[i] = a[i];
    fn(copy, n, lo, hi, nb);
    if (!is_sorted(copy, n, 0) || !is_permutation(a, copy, n)) g_sort_fails++;
    delete[] copy;
}

void run_suite(int* a, int n, int lo, int hi) {
    check_sort("bubble",   sort_bubble, a, n, 0);
    check_sort("selection",sort_selection, a, n, 0);
    check_sort("insertion",sort_insertion, a, n, 0);
    check_sort("gnome",    sort_gnome, a, n, 0);
    check_sort("shell",    sort_shell, a, n, 0);
    check_sort("merge",    sort_merge, a, n, 0);
    check_sort("quick",    sort_quick, a, n, 0);
    check_sort("heap",     sort_heap, a, n, 0);
    // comparator-aware runs (asc + desc)
    check_sort_cmp("bubble",    sort_bubble, a, n);
    check_sort_cmp("selection", sort_selection, a, n);
    check_sort_cmp("insertion", sort_insertion, a, n);
    check_sort_cmp("gnome",     sort_gnome, a, n);
    check_sort_cmp("shell",     sort_shell, a, n);
    check_sort_cmp("merge",     sort_merge, a, n);
    check_sort_cmp("quick",     sort_quick, a, n);
    check_sort_cmp("heap",      sort_heap, a, n);
    // non-comparison sorts need non-negative-ish ranges
    if (lo >= 0) {
        check_noncmp("counting", sort_counting, a, n, lo, hi);
        check_noncmp5("bucket",   sort_bucket, a, n, lo, hi, 8);
    }
    // radix handles negatives via normalization
    {
        int* copy = new int[(size_t)n];
        for (int i = 0; i < n; i++) copy[i] = a[i];
        sort_radix(copy, n);
        if (!is_sorted(copy, n, 0) || !is_permutation(a, copy, n)) g_sort_fails++;
        delete[] copy;
    }
}

} // namespace

int sort_self_test() {
    g_sort_fails = 0;
    // deterministic PRNG (LCG) so the test is reproducible
    uint32_t seed = 0x12345678;
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return (int)((seed >> 8) & 0x7FFFFFFFu);   // always non-negative
    };

    // 1) random arrays of increasing size
    int sizes[] = {0, 1, 2, 3, 7, 16, 64, 257};
    for (int si = 0; si < (int)(sizeof(sizes) / sizeof(sizes[0])); si++) {
        int n = sizes[si];
        int* a = new int[(size_t)(n > 0 ? n : 1)];
        for (int i = 0; i < n; i++) a[i] = rnd() % 2000;
        int lo = 0, hi = 1999;
        run_suite(a, n, lo, hi);
        delete[] a;
    }
    // 2) already sorted
    {
        int a[64];
        for (int i = 0; i < 64; i++) a[i] = i;
        run_suite(a, 64, 0, 63);
    }
    // 3) reverse sorted
    {
        int a[64];
        for (int i = 0; i < 64; i++) a[i] = 63 - i;
        run_suite(a, 64, 0, 63);
    }
    // 4) all duplicates
    {
        int a[48];
        for (int i = 0; i < 48; i++) a[i] = 7;
        run_suite(a, 48, 7, 7);
    }
    // 5) negatives mixed with positives (comparison sorts only)
    {
        int a[80];
        for (int i = 0; i < 80; i++) a[i] = rnd() % 401 - 200;
        check_sort("bubble", sort_bubble, a, 80, 0);
        check_sort("selection", sort_selection, a, 80, 0);
        check_sort("insertion", sort_insertion, a, 80, 0);
        check_sort("shell", sort_shell, a, 80, 0);
        check_sort("merge", sort_merge, a, 80, 0);
        check_sort("quick", sort_quick, a, 80, 0);
        check_sort("heap", sort_heap, a, 80, 0);
    }
    // 6) counting sort with negative range (must not run: guarded above),
    //    here a non-negative narrow range
    {
        int a[32];
        for (int i = 0; i < 32; i++) a[i] = rnd() % 12;
        check_noncmp("counting", sort_counting, a, 32, 0, 11);
        check_noncmp5("bucket", sort_bucket, a, 32, 0, 11, 8);
    }
    // 7) big radix range incl. large values
    {
        int a[128];
        for (int i = 0; i < 128; i++) a[i] = rnd();
        int* copy = new int[128];
        for (int i = 0; i < 128; i++) copy[i] = a[i];
        sort_radix(copy, 128);
        if (!is_sorted(copy, 128, 0) || !is_permutation(a, copy, 128)) g_sort_fails++;
        delete[] copy;
    }
    return g_sort_fails;
}

} // namespace algo
} // namespace nefu
