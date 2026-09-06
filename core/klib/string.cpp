// klib
#include "klib.h"

namespace nefu {

String::String() : buf_(0), len_(0), cap_(0) {
    reserve(16);
}

String::String(const char* s) : buf_(0), len_(0), cap_(0) {
    if (!s) s = "";
    len_ = (int)strlen(s);
    cap_ = len_ + 1;
    buf_ = new char[cap_];
    if (buf_) { memcpy(buf_, s, (size_t)len_ + 1); }
    else { len_ = 0; cap_ = 0; }
}

String::String(const String& o) : buf_(0), len_(0), cap_(0) {
    cap_ = o.cap_;
    len_ = o.len_;
    if (cap_ > 0) {
        buf_ = new char[cap_];
        if (buf_) memcpy(buf_, o.buf_, (size_t)len_ + 1);
        else { len_ = 0; cap_ = 0; }
    }
}

String::~String() {
    if (buf_) delete[] buf_;
    buf_ = 0;
}

String& String::operator=(const String& o) {
    if (this != &o) {
        reserve(o.len_ + 1);
        len_ = o.len_;
        if (buf_) { memcpy(buf_, o.buf_, (size_t)len_ + 1); }
        else { len_ = 0; }
    }
    return *this;
}

String& String::operator=(const char* s) {
    if (!s) s = "";
    int n = (int)strlen(s);
    reserve(n + 1);
    len_ = n;
    if (buf_) memcpy(buf_, s, (size_t)n + 1);
    else len_ = 0;
    return *this;
}

String& String::operator+=(const String& o) {
    return (*this) += o.c_str();
}

String& String::operator+=(char c) {
    if (len_ + 2 > cap_) grow(len_ + 2);
    if (buf_) { buf_[len_] = c; buf_[len_ + 1] = 0; len_++; }
    return *this;
}

String& String::operator+=(const char* s) {
    if (!s) return *this;
    int n = (int)strlen(s);
    if (len_ + n + 1 > cap_) grow(len_ + n + 1);
    if (buf_) { memcpy(buf_ + len_, s, (size_t)n + 1); len_ += n; }
    return *this;
}

char& String::operator[](int i) { return buf_[i]; }
const char& String::operator[](int i) const { return buf_[i]; }

void String::reserve(int cap) {
    if (cap <= cap_) return;
    char* nb = new char[cap];
    if (!nb) return;
    if (buf_ && len_ > 0) memcpy(nb, buf_, (size_t)len_);
    if (buf_) delete[] buf_;
    nb[len_] = 0;
    buf_ = nb;
    cap_ = cap;
}

void String::grow(int need) {
    int nc = cap_ > 0 ? cap_ * 2 : 16;
    while (nc < need) nc *= 2;
    reserve(nc);
}

int String::find(char c) const {
    for (int i = 0; i < len_; i++) if (buf_[i] == c) return i;
    return -1;
}

int String::find(const char* s) const {
    const char* p = strstr(buf_ ? buf_ : "", s ? s : "");
    return p ? (int)(p - buf_) : -1;
}

int String::rfind(char c) const {
    for (int i = len_ - 1; i >= 0; i--) if (buf_[i] == c) return i;
    return -1;
}

String String::substr(int start, int count) const {
    String r;
    if (!buf_ || start < 0 || start >= len_ || count <= 0) return r;
    if (start + count > len_) count = len_ - start;
    r.reserve(count + 1);
    for (int i = 0; i < count; i++) r += buf_[start + i];
    return r;
}

String operator+(const String& a, const String& b) {
    String r = a;
    r += b;
    return r;
}
String operator+(const String& a, const char* b) {
    String r = a;
    r += b;
    return r;
}
String operator+(const char* a, const String& b) {
    String r = a;
    r += b;
    return r;
}
bool operator==(const String& a, const String& b) { return strcmp(a.c_str(), b.c_str()) == 0; }
bool operator==(const String& a, const char* b) { return strcmp(a.c_str(), b ? b : "") == 0; }

bool operator!=(const String& a, const String& b) {
    if (a.len() != b.len()) return true;
    return memcmp(a.c_str(), b.c_str(), (size_t)a.len()) != 0;
}

bool operator!=(const String& a, const char* b) {
    if (!b) return true;
    int bl = 0;
    while (b[bl]) bl++;
    if (a.len() != bl) return true;
    return memcmp(a.c_str(), b, (size_t)bl) != 0;
}

} // namespace nefu
