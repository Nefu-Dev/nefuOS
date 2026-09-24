// nefuOS search algorithm library — implementation & self test
// See search.h for API and complexity documentation.
#include "search.h"
#include <string.h>

namespace nefu {
namespace algo {

// ---------------------------------------------------------------------
// linear search
// ---------------------------------------------------------------------
int search_linear(const int* a, int n, int key) {
    for (int i = 0; i < n; i++)
        if (a[i] == key) return i;
    return -1;
}

// ---------------------------------------------------------------------
// binary search (ascending array)
//   returns match index, or -(insertion point + 1)
// ---------------------------------------------------------------------
int search_binary(const int* a, int n, int key) {
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;      // avoid lo+hi overflow
        if (a[mid] == key) return mid;
        if (a[mid] < key) lo = mid + 1;
        else              hi = mid - 1;
    }
    return -(lo + 1);                      // lo is the insertion point
}

// ---------------------------------------------------------------------
// ternary search (recursive two-pivot split)
// ---------------------------------------------------------------------
int search_ternary(const int* a, int lo, int hi, int key) {
    if (hi < lo) return -1;
    if (hi - lo < 2) {                     // tiny window: direct scan
        for (int i = lo; i <= hi; i++) if (a[i] == key) return i;
        return -1;
    }
    int m1 = lo + (hi - lo) / 3;
    int m2 = hi - (hi - lo) / 3;
    if (key < a[m1])      return search_ternary(a, lo, m1 - 1, key);
    if (key > a[m2])      return search_ternary(a, m2 + 1, hi, key);
    if (key == a[m1])     return m1;
    if (key == a[m2])     return m2;
    return search_ternary(a, m1 + 1, m2 - 1, key);
}

// ---------------------------------------------------------------------
// interpolation search
// ---------------------------------------------------------------------
int search_interp(const int* a, int n, int key) {
    int lo = 0, hi = n - 1;
    while (lo <= hi && key >= a[lo] && key <= a[hi]) {
        if (a[lo] == a[hi]) {              // flat segment: direct check
            return a[lo] == key ? lo : -1;
        }
        // probe position proportional to where key sits in [a[lo], a[hi]]
        int64_t span = (int64_t)a[hi] - a[lo];
        int pos = lo + (int)((int64_t)(key - a[lo]) * (hi - lo) / span);
        if (pos < lo) pos = lo;
        if (pos > hi) pos = hi;
        if (a[pos] == key) return pos;
        if (a[pos] < key) lo = pos + 1;
        else              hi = pos - 1;
    }
    return -1;
}

// ---------------------------------------------------------------------
// exponential search
// ---------------------------------------------------------------------
int search_exp(const int* a, int n, int key) {
    if (n <= 0) return -1;
    if (a[0] == key) return 0;
    int bound = 1;
    while (bound < n && a[bound] <= key) bound *= 2;   // find the window
    int lo = bound / 2;
    int hi = bound < n ? bound : n - 1;
    // binary search inside [lo, hi]
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] == key) return mid;
        if (a[mid] < key) lo = mid + 1;
        else              hi = mid - 1;
    }
    return -1;
}

// ---------------------------------------------------------------------
// naive string matching
// ---------------------------------------------------------------------
int strfind_naive(const char* text, int n, const char* pat, int m) {
    if (m <= 0) return 0;
    if (m > n) return -1;
    for (int i = 0; i <= n - m; i++) {
        int j = 0;
        while (j < m && text[i + j] == pat[j]) j++;
        if (j == m) return i;
    }
    return -1;
}

// ---------------------------------------------------------------------
// Knuth-Morris-Pratt
// ---------------------------------------------------------------------
static void kmp_failure(const char* pat, int m, int* pi) {
    // pi[i] = length of the longest proper prefix of pat[0..i] that is
    // also a suffix of pat[0..i]
    pi[0] = 0;
    int k = 0;
    for (int i = 1; i < m; i++) {
        while (k > 0 && pat[i] != pat[k]) k = pi[k - 1];
        if (pat[i] == pat[k]) k++;
        pi[i] = k;
    }
}

int strfind_kmp(const char* text, int n, const char* pat, int m) {
    if (m <= 0) return 0;
    if (m > n) return -1;
    int* pi = new int[(size_t)m];
    if (!pi) return -1;
    kmp_failure(pat, m, pi);
    int k = 0;                              // matched prefix length
    int found = -1;
    for (int i = 0; i < n && found < 0; i++) {
        while (k > 0 && text[i] != pat[k]) k = pi[k - 1];
        if (text[i] == pat[k]) k++;
        if (k == m) { found = i - m + 1; break; }
    }
    delete[] pi;
    return found;
}

// ---------------------------------------------------------------------
// Rabin-Karp (rolling polynomial hash)
// ---------------------------------------------------------------------
int strfind_rk(const char* text, int n, const char* pat, int m) {
    if (m <= 0) return 0;
    if (m > n) return -1;
    const uint32_t BASE = 257u;
    const uint32_t MOD = 1000000007u;
    // precompute BASE^(m-1) mod MOD
    uint32_t hp = 0, ht = 0, power = 1;
    for (int i = 0; i < m - 1; i++) power = (uint32_t)(((uint64_t)power * BASE) % MOD);
    for (int i = 0; i < m; i++) {
        hp = (uint32_t)(((uint64_t)hp * BASE + (uint32_t)(uint8_t)pat[i]) % MOD);
        ht = (uint32_t)(((uint64_t)ht * BASE + (uint32_t)(uint8_t)text[i]) % MOD);
    }
    for (int i = 0; i <= n - m; i++) {
        if (ht == hp) {
            // hash match: verify byte by byte (guards against collisions)
            int j = 0;
            while (j < m && text[i + j] == pat[j]) j++;
            if (j == m) return i;
        }
        // roll the window one character forward
        if (i + m < n) {
            ht = (uint32_t)((ht + MOD - ((uint64_t)(uint32_t)(uint8_t)text[i] * power) % MOD) % MOD);
            ht = (uint32_t)(((uint64_t)ht * BASE + (uint32_t)(uint8_t)text[i + m]) % MOD);
        }
    }
    return -1;
}

// ---------------------------------------------------------------------
// self test
// ---------------------------------------------------------------------
namespace {
int g_search_fails = 0;

void expect(const char* what, bool ok) {
    if (!ok) g_search_fails++;
    (void)what;
}

} // namespace

int search_self_test() {
    g_search_fails = 0;

    // --- array searches ---
    int asc[16];
    for (int i = 0; i < 16; i++) asc[i] = i * 3;      // 0,3,6,...,45
    for (int i = 0; i < 16; i++) {
        expect("linear", search_linear(asc, 16, i * 3) == i);
        expect("binary", search_binary(asc, 16, i * 3) == i);
        expect("ternary", search_ternary(asc, 0, 15, i * 3) == i);
        expect("interp", search_interp(asc, 16, i * 3) == i);
        expect("exp", search_exp(asc, 16, i * 3) == i);
    }
    // missing keys
    expect("linear-miss", search_linear(asc, 16, 1) == -1);
    expect("binary-miss", search_binary(asc, 16, 2) < 0);           // negative sentinel
    expect("ternary-miss", search_ternary(asc, 0, 15, 100) == -1);
    expect("interp-miss", search_interp(asc, 16, 100) == -1);
    expect("exp-miss", search_exp(asc, 16, 100) == -1);
    // boundaries
    expect("binary-lo", search_binary(asc, 16, -1) == -1);
    expect("exp-empty", search_exp(asc, 0, 5) == -1);
    expect("ternary-empty", search_ternary(asc, 5, 4, 5) == -1);
    // insertion point semantics
    expect("binary-ins", search_binary(asc, 16, 1) == -(1 + 1));    // insert at 1

    // --- string matching ---
    const char* text = "the quick brown fox jumps over the lazy dog";
    int n = (int)strlen(text);
    const char* p1 = "quick";
    const char* p2 = "dog";
    const char* p3 = "xyz";
    const char* p4 = "the";
    const char* p5 = "o";
    expect("naive1", strfind_naive(text, n, p1, 5) == 4);
    expect("kmp1",   strfind_kmp(text, n, p1, 5) == 4);
    expect("rk1",    strfind_rk(text, n, p1, 5) == 4);
    expect("naive2", strfind_naive(text, n, p2, 3) == 40);
    expect("kmp2",   strfind_kmp(text, n, p2, 3) == 40);
    expect("rk2",    strfind_rk(text, n, p2, 3) == 40);
    expect("naive3", strfind_naive(text, n, p3, 3) == -1);
    expect("kmp3",   strfind_kmp(text, n, p3, 3) == -1);
    expect("rk3",    strfind_rk(text, n, p3, 3) == -1);
    // overlapping patterns exercise the KMP failure table
    const char* ov = "aaaaab";
    expect("kmp-overlap", strfind_kmp(ov, 6, "aaaab", 5) == 1);
    expect("rk-overlap",  strfind_rk(ov, 6, "aaaab", 5) == 1);
    // empty / degenerate
    expect("kmp-empty", strfind_kmp("abc", 3, "", 0) == 0);
    expect("rk-empty",  strfind_rk("abc", 3, "", 0) == 0);
    expect("kmp-too-long", strfind_kmp("ab", 2, "abc", 3) == -1);
    // binary pattern for hash sanity
    {
        const char bin[] = { (char)0x00, (char)0xFF, (char)0x10, (char)0x41, 0 };
        expect("rk-bin", strfind_rk(bin, 4, bin + 1, 3) == 1);
        expect("kmp-bin", strfind_kmp(bin, 4, bin + 1, 3) == 1);
        expect("naive-bin", strfind_naive(bin, 4, bin + 1, 3) == 1);
    }
    return g_search_fails;
}

} // namespace algo
} // namespace nefu
