// klib memory functions & globals new/delete
#include "klib.h"
#include "../platform.h"

extern "C" {

__attribute__((noinline)) void* memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dst;
}

__attribute__((noinline)) void* memmove(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s) { while (n--) *d++ = *s++; }
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}

__attribute__((noinline)) void* memset(void* dst, int c, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    while (n--) *d++ = (uint8_t)c;
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
    size_t n = 0;
    while (s[n]) n++;
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
