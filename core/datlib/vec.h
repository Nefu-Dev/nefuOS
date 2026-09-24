// nefuOS data-types library — generic dynamic array (vec)
// 泛型动态数组：自动扩容的连续存储容器，是 C++ 里最常用的数据结构之一。
// 与 STL 的 std::vector 功能对应，但完全从零实现（不依赖 STL），
// 适合教学：默认构造、拷贝、扩容策略、下标访问、迭代与缩容。
//
// 扩容策略：容量翻倍（2x），均摊 O(1) 追加；缩容阈值 1/4（容量降到
// 1/2），避免频繁抖动。所有动态内存使用 new[]/delete[]（无 malloc），
// 与 nefuOS 内核的裸机编译约定一致（-ffreestanding 下 <stdlib.h>
// 不声明 malloc）。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace dt {

// 泛型动态数组。T 需要具备默认构造与拷贝赋值能力。
template <typename T>
class vec {
public:
    typedef T        value_type;
    typedef T*       iterator;
    typedef const T* const_iterator;

    // 构造与析构 ------------------------------------------------
    vec() : data_(0), size_(0), cap_(0) {}
    explicit vec(int reserve_n) : data_(0), size_(0), cap_(0) { reserve(reserve_n); }
    ~vec() { clear(); if (data_) { delete[] data_; data_ = 0; } }

    // 拷贝构造与赋值（深拷贝，注意三/五法则）
    vec(const vec& o) : data_(0), size_(0), cap_(0) { assign(o); }
    vec& operator=(const vec& o) { if (this != &o) assign(o); return *this; }

    // 容量与大小 ------------------------------------------------
    int size() const { return size_; }
    int capacity() const { return cap_; }
    bool empty() const { return size_ == 0; }

    void reserve(int n) {
        if (n <= cap_) return;
        T* nd = new T[n];
        for (int i = 0; i < size_; i++) nd[i] = data_[i];
        if (data_) delete[] data_;
        data_ = nd;
        cap_ = n;
    }
    // 缩容：把容量收缩到当前大小（省内存用）
    void shrink_to_fit() {
        if (cap_ == size_) return;
        T* nd = size_ ? new T[size_] : 0;
        for (int i = 0; i < size_; i++) nd[i] = data_[i];
        if (data_) delete[] data_;
        data_ = nd;
        cap_ = size_;
    }

    // 元素访问 ------------------------------------------------
    T& operator[](int i) { return data_[i]; }
    const T& operator[](int i) const { return data_[i]; }
    T& at(int i) { return data_[i]; }          // 教学版：不做越界检查
    T& front() { return data_[0]; }
    T& back() { return data_[size_ - 1]; }
    T* data() { return data_; }
    const T* data() const { return data_; }

    // 修改 ----------------------------------------------------
    void push_back(const T& v) {
        if (size_ >= cap_) {
            int nc = cap_ ? cap_ * 2 : 4;     // 翻倍扩容，初始 4
            reserve(nc);
        }
        data_[size_++] = v;
    }
    void pop_back() { if (size_) size_--; }   // 简单弹出（不调用析构）
    void clear() { size_ = 0; }               // 仅置零，容量保留

    void insert(int idx, const T& v) {
        if (idx < 0) idx = 0;
        if (idx > size_) idx = size_;
        push_back(v);
        // 把新元素挪到 idx 位置（向右平移中间段）
        for (int i = size_ - 1; i > idx; i--) data_[i] = data_[i - 1];
        data_[idx] = v;
    }
    void erase(int idx) {
        if (idx < 0 || idx >= size_) return;
        for (int i = idx; i < size_ - 1; i++) data_[i] = data_[i + 1];
        size_--;
    }
    void resize(int n) {
        if (n > cap_) reserve(n);
        // 教学版：扩容后新元素保持默认构造前的未初始化状态，仅调整计数
        size_ = n;
    }

    // 迭代 ----------------------------------------------------
    iterator begin() { return data_; }
    iterator end() { return data_ + size_; }
    const_iterator begin() const { return data_; }
    const_iterator end() const { return data_ + size_; }

    // 查找 ----------------------------------------------------
    // 返回第一个等于 v 的下标；不存在返回 -1
    int index_of(const T& v) const {
        for (int i = 0; i < size_; i++) if (data_[i] == v) return i;
        return -1;
    }
    // 计数：值为 v 的元素个数
    int count(const T& v) const {
        int c = 0;
        for (int i = 0; i < size_; i++) if (data_[i] == v) c++;
        return c;
    }
    // 二分查找（要求已按 < 排序）；返回下标或 -1
    int binary_search(const T& v) const {
        int lo = 0, hi = size_ - 1;
        while (lo <= hi) {
            int mid = lo + (hi - lo) / 2;
            if (data_[mid] == v) return mid;
            if (data_[mid] < v) lo = mid + 1; else hi = mid - 1;
        }
        return -1;
    }

    // 排序（升序，简单插入排序；教学演示）
    void sort_asc() {
        for (int i = 1; i < size_; i++) {
            T key = data_[i];
            int j = i - 1;
            while (j >= 0 && data_[j] > key) { data_[j + 1] = data_[j]; j--; }
            data_[j + 1] = key;
        }
    }
    // 反转
    void reverse() {
        for (int i = 0, j = size_ - 1; i < j; i++, j--) {
            T tmp = data_[i]; data_[i] = data_[j]; data_[j] = tmp;
        }
    }

private:
    void assign(const vec& o) {
        clear();
        if (data_) { delete[] data_; data_ = 0; }
        size_ = o.size_; cap_ = o.cap_;
        if (cap_) data_ = new T[cap_];
        for (int i = 0; i < size_; i++) data_[i] = o.data_[i];
    }

    T* data_;
    int size_;
    int cap_;
};

int vec_self_test();

} // namespace dt
} // namespace nefu
