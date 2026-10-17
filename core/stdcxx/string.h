// nefuOS stdcxx - <string>
// A std::string-like dynamic string on the nefuOS kernel heap. Built on the
// same underlying char buffer approach as nefu::String (but self-contained).
#pragma once
#include "_core.h"
#include <string.h>
namespace nefu {
namespace stdx {

class string {
public:
    string() : d_(0), n_(0), cap_(0) {}
    string(const char* s) : d_(0), n_(0), cap_(0) { if (s) assign(s, strlen(s)); }
    string(const char* s, size_t len) : d_(0), n_(0), cap_(0) { assign(s, len); }
    string(const string& o) : d_(0), n_(0), cap_(0) { assign(o.d_, o.n_); }
    string& operator=(const string& o) { if (this != &o) assign(o.d_, o.n_); return *this; }
    string& operator=(const char* s) { if (s) assign(s, strlen(s)); else clear(); return *this; }
    ~string() { clear(); if (d_) deallocate(d_); }

    size_t size() const { return n_; }
    size_t length() const { return n_; }
    bool empty() const { return n_ == 0; }
    size_t capacity() const { return cap_; }

    const char* c_str() const { return d_ ? d_ : ""; }
    const char* data() const { return d_ ? d_ : ""; }

    char& operator[](size_t i) { return d_[i]; }
    const char& operator[](size_t i) const { return d_[i]; }
    char& at(size_t i) { return d_[i]; }
    char& front() { return d_[0]; }
    char& back() { return d_[n_ - 1]; }
    char* begin() { return d_; }
    char* end() { return d_ + n_; }
    const char* begin() const { return d_; }
    const char* end() const { return d_ + n_; }

    void reserve(size_t c) {
        if (c <= cap_) return;
        size_t nc = cap_ ? cap_ : 16; while (nc < c) nc *= 2;
        char* nd = (char*)allocate(nc);
        if (n_) { memcpy(nd, d_, n_); deallocate(d_); }
        d_ = nd; cap_ = nc;
    }
    void assign(const char* s, size_t len) {
        reserve(len + 1);
        memcpy(d_, s, len); d_[len] = 0; n_ = len;
    }
    void clear() { if (d_) d_[0] = 0; n_ = 0; }

    void push_back(char c) { reserve(n_ + 2); d_[n_++] = c; d_[n_] = 0; }
    void pop_back() { if (n_) { d_[--n_] = 0; } }

    string& append(const char* s, size_t len) {
        reserve(n_ + len + 1);
        memcpy(d_ + n_, s, len); n_ += len; d_[n_] = 0;
        return *this;
    }
    string& append(const string& o) { return append(o.d_, o.n_); }
    string& append(const char* s) { return s ? append(s, strlen(s)) : *this; }
    string& operator+=(const char* s) { return append(s); }
    string& operator+=(const string& o) { return append(o); }
    string& operator+=(char c) { push_back(c); return *this; }

    size_t find(char c) const { for (size_t i = 0; i < n_; ++i) if (d_[i] == c) return i; return (size_t)-1; }
    size_t find(const char* s) const {
        if (!s || !*s) return 0;
        size_t sl = strlen(s); if (sl > n_) return (size_t)-1;
        for (size_t i = 0; i + sl <= n_; ++i) if (memcmp(d_ + i, s, sl) == 0) return i;
        return (size_t)-1;
    }
    int compare(const char* s) const { return strcmp(c_str(), s ? s : ""); }
    int compare(const string& o) const { return strcmp(c_str(), o.c_str()); }
    string substr(size_t pos, size_t len = (size_t)-1) const {
        if (pos >= n_) return string();
        if (len == (size_t)-1 || pos + len > n_) len = n_ - pos;
        return string(d_ + pos, len);
    }

    bool operator==(const char* s) const { return compare(s) == 0; }
    bool operator==(const string& o) const { return compare(o) == 0; }
    bool operator!=(const char* s) const { return compare(s) != 0; }
    bool operator!=(const string& o) const { return compare(o) != 0; }
    bool operator<(const string& o) const { return compare(o) < 0; }

private:
    char*   d_;
    size_t  n_;
    size_t  cap_;
};

inline string operator+(const string& a, const string& b) { string r(a); r += b; return r; }
inline string operator+(const string& a, const char* b)   { string r(a); r += b; return r; }
inline string operator+(const char* a, const string& b)   { string r(a); r += b; return r; }

} // namespace stdx
} // namespace nefu
