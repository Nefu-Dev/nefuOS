// nefuOS base library：memory、string、list、format
// supports both host（Win32）and bare（freestanding）compile
#pragma once
#include <stdint.h>
#include <stddef.h>

// ===================== global new/delete（use kalloc/kfree，） =====================
void* operator new(size_t sz);
void* operator new[](size_t sz);
void  operator delete(void* p) noexcept;
void  operator delete[](void* p) noexcept;
inline void* operator new(size_t sz, void* p) noexcept { (void)sz; return p; }
inline void* operator new[](size_t sz, void* p) noexcept { (void)sz; return p; }
inline void  operator delete(void* p, void* place) noexcept { (void)p; (void)place; }
inline void  operator delete[](void* p, void* place) noexcept { (void)p; (void)place; }

namespace nefu {

// ===================== memory（extern "C"，for compiler and library） =====================
extern "C" {
void*  memcpy(void* dst, const void* src, size_t n);
void*  memmove(void* dst, const void* src, size_t n);
void*  memset(void* dst, int c, size_t n);
int    memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);
int    strcmp(const char* a, const char* b);
int    strncmp(const char* a, const char* b, size_t n);
char*  strcpy(char* dst, const char* src);
char*  strncpy(char* dst, const char* src, size_t n);
char*  strcat(char* dst, const char* src);
char*  strchr(const char* s, int c);
char*  strstr(const char* hay, const char* needle);
int    atoi(const char* s);
}

// ===================== memory（extern "C"，for compiler and library） =====================
class String {
public:
    String();
    String(const char* s);
    String(const String& o);
    ~String();
    String& operator=(const String& o);
    String& operator=(const char* s);
    String& operator+=(const String& o);
    String& operator+=(char c);
    String& operator+=(const char* s);
    char& operator[](int i);
    const char& operator[](int i) const;
    const char* c_str() const { return buf_; }
    char* data() { return buf_; }
    int len() const { return len_; }
    bool empty() const { return len_ == 0; }
    int find(char c) const;
    int find(const char* s) const;
    int rfind(char c) const;
    String substr(int start, int count) const;
    void clear() { len_ = 0; if (buf_) buf_[0] = 0; }
    void reserve(int cap);
private:
    void grow(int need);
    char* buf_;
    int len_;
    int cap_;
};

String operator+(const String& a, const String& b);
String operator+(const String& a, const char* b);
String operator+(const char* a, const String& b);
bool operator==(const String& a, const String& b);
bool operator==(const String& a, const char* b);
bool operator!=(const String& a, const String& b);
bool operator!=(const String& a, const char* b);

// ===================== list =====================
template <typename T>
class List {
public:
    List() : data_(0), size_(0), cap_(0) {}
    List(const List& o) : data_(0), size_(0), cap_(0) { copy_from(o); }
    ~List() { release(); }
    List& operator=(const List& o) {
        if (this != &o) { release(); copy_from(o); }
        return *this;
    }
    int size() const { return size_; }
    bool empty() const { return size_ == 0; }
    T& operator[](int i) { return data_[i]; }
    const T& operator[](int i) const { return data_[i]; }
    void push(const T& v) { if (size_ >= cap_) grow(); data_[size_++] = v; }
    void insert(int i, const T& v) {
        if (i < 0) i = 0;
        if (i > size_) i = size_;
        if (size_ >= cap_) grow();
        for (int j = size_; j > i; j--) data_[j] = data_[j - 1];
        data_[i] = v;
        size_++;
    }
    T pop() { T v = data_[--size_]; return v; }
    void remove(int i) {
        if (i >= 0 && i < size_) {
            for (int j = i; j < size_ - 1; j++) data_[j] = data_[j + 1];
            size_--;
        }
    }
    void clear() { size_ = 0; }
    void erase_all() { release(); }
    T* data() { return data_; }
private:
    void release() { if (data_) delete[] data_; data_ = 0; size_ = 0; cap_ = 0; }
    void copy_from(const List& o) {
        size_ = o.size_; cap_ = o.cap_;
        if (cap_ > 0) {
            data_ = new T[cap_];
            if (data_) { for (int i = 0; i < size_; i++) data_[i] = o.data_[i]; }
            else { size_ = 0; }
        } else data_ = 0;
    }
    void grow() {
        int nc = cap_ > 0 ? cap_ * 2 : 8;
        T* nd = new T[nc];
        if (!nd) return;
        for (int i = 0; i < size_; i++) nd[i] = data_[i];
        if (data_) delete[] data_;
        data_ = nd; cap_ = nc;
    }
    T* data_;
    int size_;
    int cap_;
};

// ===================== format =====================
int  ksprintf(char* buf, size_t bufsz, const char* fmt, ...);
void klogf(const char* fmt, ...);   // format then output to debug channel

} // namespace nefu
