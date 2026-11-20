/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// klib memory functions & globals new/delete
#include "klib.h"
#include "../platform.h"
#include <stdio.h>
#include <windows.h>

extern "C" {

// The bare TCP receive path copies every inbound byte twice (segment into
// rxbuf, then rxbuf into the TLS buffer, then a memmove to compact the
// remainder). A byte-at-a-time loop made a 3.36 MB JS bundle take minutes
// under QEMU TCG. x86-64 allows unaligned 64-bit loads/stores (strlen below
// already relies on that), and may_alias stops -O2 type-based alias analysis
// from assuming the destination byte buffer is untouched.
typedef uint64_t nfu_word __attribute__((may_alias));

__attribute__((noinline)) void* memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    size_t w = n >> 3;
    if (w) {
        nfu_word* dw = (nfu_word*)d;
        const nfu_word* sw = (const nfu_word*)s;
        do { *dw++ = *sw++; } while (--w);
        d = (uint8_t*)dw;
        s = (const uint8_t*)sw;
    }
    n &= 7;
    while (n--) *d++ = *s++;
    return dst;
}

__attribute__((noinline)) void* memmove(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        // Forward copy. The read pointer always stays ahead of the write
        // pointer, so 8-byte words never read bytes we already overwrote.
        size_t w = n >> 3;
        if (w) {
            nfu_word* dw = (nfu_word*)d;
            const nfu_word* sw = (const nfu_word*)s;
            do { *dw++ = *sw++; } while (--w);
            d = (uint8_t*)dw;
            s = (const uint8_t*)sw;
        }
        n &= 7;
        while (n--) *d++ = *s++;
    } else {
        // Backward copy from the tail, required when dst starts after src.
        size_t left = n;
        while (left >= 8) {
            left -= 8;
            *(nfu_word*)(d + left) = *(const nfu_word*)(s + left);
        }
        while (left--) d[left] = s[left];
    }
    return dst;
}

__attribute__((noinline)) void* memset(void* dst, int c, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    uint8_t b = (uint8_t)c;
    uint64_t v = 0x0101010101010101ULL * (uint64_t)b;
    size_t w = n >> 3;
    if (w) {
        nfu_word* dw = (nfu_word*)d;
        do { *dw++ = v; } while (--w);
        d = (uint8_t*)dw;
    }
    n &= 7;
    while (n--) *d++ = b;
    return dst;
}

__attribute__((noinline)) int memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* x = (const uint8_t*)a;
    const uint8_t* y = (const uint8_t*)b;
    while (n--) {
        if (*x != *y) return (int)*x - (int)*y;
        x++; y++;
    }
    return 0;
}

__attribute__((noinline)) size_t strlen(const char* s) {
    const size_t cap = (size_t)1 << 23;   // 8MB scan cap: corrupt non-NUL buffers return instead of hanging/crashing
    size_t n = 0;
    // 8-byte word scan (unaligned reads OK on x64) for ~8x speedup. A word read
    // must never straddle a page boundary (the next page may be unmapped), so
    // the last few bytes of a page are stepped one at a time and the word scan
    // resumes on the next page. Never stop early: a truncated length silently
    // corrupts every caller that measures a string (quickjs atoms, class
    // names, URL parsing).
    while (n < cap) {
        uintptr_t a = (uintptr_t)(s + n);
        if ((a & 0xFFF) <= 0xFF8) {
            uint64_t v = *(const uint64_t*)a;
            uint64_t m = (v - 0x0101010101010101ULL) & ~v & 0x8080808080808080ULL;
            if (m) return n + (size_t)(__builtin_ctzll(m) >> 3);
            n += 8;
        } else {
            if (!s[n]) return n;   // page tail: byte step, stays inside the page
            n++;
        }
    }
    return n;
}

__attribute__((noinline)) int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

__attribute__((noinline)) int strncmp(const char* a, const char* b, size_t n) {
    while (n-- && *a && *a == *b) { a++; b++; }
    if (n == (size_t)-1) return 0;
    return (uint8_t)*a - (uint8_t)*b;
}

__attribute__((noinline)) char* strcpy(char* dst, const char* src) {
    char* d = dst;
    while ((*d++ = *src++)) {}
    return dst;
}

__attribute__((noinline)) char* strncpy(char* dst, const char* src, size_t n) {
    // NOTE: a plain while() version is mis-optimized by mingw g++ -O2 into an
    // infinite zero-fill loop (terminator rdx=r9-1 never reached) whenever src
    // has no NUL within n bytes.  volatile stores defeat that expansion.
    volatile char* d = dst;
    const char* s = src;
    size_t m = n;
    while (m > 0 && *s) { *d++ = *s++; m--; }
    while (m > 0) { *d++ = 0; m--; }
    return dst;
}

__attribute__((noinline)) char* strcat(char* dst, const char* src) {
    char* d = dst;
    while (*d) d++;
    while ((*d++ = *src++)) {}
    return dst;
}

char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (char)c == 0 ? (char*)s : 0;
}

char* strrchr(const char* s, int c) {
    const char* last = 0;
    for (; *s; s++) {
        if (*s == (char)c) last = s;
    }
    return (char)c == 0 ? (char*)s : (char*)last;
}
char* strstr(const char* hay, const char* needle) {
    if (!*needle) return (char*)hay;
    for (; *hay; hay++) {
        const char* h = hay;
        const char* n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char*)hay;
    }
    return 0;
}

int atoi(const char* s) {
    int v = 0, sign = 1;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v * sign;
}

} // extern "C"

namespace nefu {

void* operator_new_impl(size_t sz) {
    return kalloc(sz);
}

} // namespace nefu

void* operator new(size_t sz) { return nefu::operator_new_impl(sz); }
void* operator new[](size_t sz) { return nefu::operator_new_impl(sz); }
void operator delete(void* p) noexcept { nefu::kfree(p); }
void operator delete[](void* p) noexcept { nefu::kfree(p); }
void operator delete(void* p, size_t sz) noexcept { (void)sz; nefu::kfree(p); }
void operator delete[](void* p, size_t sz) noexcept { (void)sz; nefu::kfree(p); }

