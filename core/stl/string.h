// nefuOS STL - string implementation
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace stl {

class string {
private:
    char* data_;
    size_t size_;
    size_t capacity_;
    
    void grow(size_t min_cap) {
        size_t new_cap = capacity_ * 2;
        if (new_cap < min_cap) new_cap = min_cap;
        char* new_data = (char*)kalloc(new_cap);
        if (!new_data) return;
        memcpy(new_data, data_, size_ + 1);
        kfree(data_);
        data_ = new_data;
        capacity_ = new_cap;
    }
    
public:
    typedef size_t size_type;
    static const size_type npos = (size_type)-1;
    
    string() : data_((char*)kalloc(1)), size_(0), capacity_(1) {
        data_[0] = 0;
    }
    
    string(const char* s) : data_(0), size_(0), capacity_(0) {
        size_t len = strlen(s);
        capacity_ = len + 1;
        data_ = (char*)kalloc(capacity_);
        memcpy(data_, s, len + 1);
        size_ = len;
    }
    
    string(const string& other) : data_(0), size_(0), capacity_(0) {
        capacity_ = other.size_ + 1;
        data_ = (char*)kalloc(capacity_);
        memcpy(data_, other.data_, other.size_ + 1);
        size_ = other.size_;
    }
    
    ~string() {
        kfree(data_);
    }
    
    string& operator=(const string& other) {
        if (this == &other) return *this;
        if (capacity_ < other.size_ + 1) {
            kfree(data_);
            capacity_ = other.size_ + 1;
            data_ = (char*)kalloc(capacity_);
        }
        memcpy(data_, other.data_, other.size_ + 1);
        size_ = other.size_;
        return *this;
    }
    
    string& operator=(const char* s) {
        size_t len = strlen(s);
        if (capacity_ < len + 1) {
            kfree(data_);
            capacity_ = len + 1;
            data_ = (char*)kalloc(capacity_);
        }
        memcpy(data_, s, len + 1);
        size_ = len;
        return *this;
    }
    
    size_type size() const { return size_; }
    size_type length() const { return size_; }
    bool empty() const { return size_ == 0; }
    const char* c_str() const { return data_; }
    
    char& operator[](size_type i) { return data_[i]; }
    const char& operator[](size_type i) const { return data_[i]; }
    
    string& operator+=(const string& other) {
        if (size_ + other.size_ + 1 > capacity_) {
            grow(size_ + other.size_ + 1);
        }
        memcpy(data_ + size_, other.data_, other.size_);
        size_ += other.size_;
        data_[size_] = 0;
        return *this;
    }
    
    string& operator+=(char c) {
        if (size_ + 2 > capacity_) {
            grow(size_ + 2);
        }
        data_[size_++] = c;
        data_[size_] = 0;
        return *this;
    }
    
    string operator+(const string& other) const {
        string result = *this;
        result += other;
        return result;
    }
    
    bool operator==(const string& other) const {
        if (size_ != other.size_) return false;
        return strcmp(data_, other.data_) == 0;
    }
    
    bool operator!=(const string& other) const {
        return !(*this == other);
    }
    
    bool operator<(const string& other) const {
        return strcmp(data_, other.data_) < 0;
    }
    
    int compare(const string& other) const {
        return strcmp(data_, other.data_);
    }
    
    size_type find(const string& str, size_type pos = 0) const {
        for (size_type i = pos; i <= size_ - str.size_; i++) {
            bool match = true;
            for (size_type j = 0; j < str.size_; j++) {
                if (data_[i + j] != str.data_[j]) {
                    match = false;
                    break;
                }
            }
            if (match) return i;
        }
        return npos;
    }
    
    size_type find(char c, size_type pos = 0) const {
        for (size_type i = pos; i < size_; i++) {
            if (data_[i] == c) return i;
        }
        return npos;
    }
    
    string substr(size_type pos = 0, size_type len = npos) const {
        if (pos > size_) pos = size_;
        if (len == npos || pos + len > size_) len = size_ - pos;
        string result;
        result.grow(len + 1);
        memcpy(result.data_, data_ + pos, len);
        result.data_[len] = 0;
        result.size_ = len;
        return result;
    }
    
    void clear() {
        size_ = 0;
        data_[0] = 0;
    }
    
    void push_back(char c) {
        *this += c;
    }
    
    void pop_back() {
        if (size_ > 0) {
            size_--;
            data_[size_] = 0;
        }
    }
};

inline string operator+(const string& lhs, const string& rhs) {
    return lhs + rhs;
}

inline string operator+(const char* lhs, const string& rhs) {
    return string(lhs) + rhs;
}

inline string operator+(char lhs, const string& rhs) {
    string result;
    result += lhs;
    result += rhs;
    return result;
}

} // namespace stl
} // namespace nefu
