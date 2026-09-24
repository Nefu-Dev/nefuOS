// nefuOS data-types library — generic ring buffer (ringbuf)
// 泛型环形缓冲：固定容量的先进先出队列，头尾指针在环上循环。
// 用于生产者-消费者、数据流缓冲、滚动窗口等场景。
// 本实现维护 (head, tail) 两个下标；空 = head==tail；满 = (tail+1)%cap==head，
// 因此最多存放 cap-1 个元素（牺牲一格区分空/满）。
// 所有操作 O(1)，无动态分配（容量构造时一次性分配）。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace dt {

template <typename T>
class ringbuf {
public:
    explicit ringbuf(int cap) : data_(0), cap_(cap), head_(0), tail_(0), count_(0) {
        if (cap_ < 2) cap_ = 2;
        data_ = new T[cap_];
    }
    ~ringbuf() { if (data_) { delete[] data_; data_ = 0; } }

    // 禁止拷贝（内部指针所有权简单化）
    ringbuf(const ringbuf&) = delete;
    ringbuf& operator=(const ringbuf&) = delete;

    bool empty() const { return count_ == 0; }
    bool full() const { return count_ == cap_ - 1; }   // 留一格
    int size() const { return count_; }
    int capacity() const { return cap_; }

    // 尾部入队；满则返回 false
    bool push(const T& v) {
        if (full()) return false;
        data_[tail_] = v;
        tail_ = (tail_ + 1) % cap_;
        count_++;
        return true;
    }
    // 头部出队；空则返回 false
    bool pop(T& out) {
        if (empty()) return false;
        out = data_[head_];
        head_ = (head_ + 1) % cap_;
        count_--;
        return true;
    }
    // 只看头部不取出；空则返回 false
    bool peek(T& out) const {
        if (empty()) return false;
        out = data_[head_];
        return true;
    }
    // 覆盖式入队：满时丢弃最旧元素再入队
    void push_overwrite(const T& v) {
        if (full()) { head_ = (head_ + 1) % cap_; count_--; }
        data_[tail_] = v;
        tail_ = (tail_ + 1) % cap_;
        count_++;
    }
    // 清空
    void clear() { head_ = 0; tail_ = 0; count_ = 0; }

    // 按 FIFO 顺序访问第 i 个元素（0 = 最旧）
    T& at(int i) { return data_[(head_ + i) % cap_]; }
    const T& at(int i) const { return data_[(head_ + i) % cap_]; }

private:
    T*  data_;
    int cap_;
    int head_;    // 下一个出队位置
    int tail_;    // 下一个入队位置
    int count_;   // 当前元素数
};

int ringbuf_self_test();

} // namespace dt
} // namespace nefu
