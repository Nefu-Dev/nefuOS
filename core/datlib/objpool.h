// nefuOS data-types library — object pool (objpool)
// 对象池：预分配一批固定大小节点，空闲链表复用，避免频繁 new/delete
// 造成的内存碎片与分配开销。常用于粒子系统、消息队列、事件循环、
// 网络连接复用等高频小对象场景。线程安全不在本教学版范围。
// 本实现节点为固定 64 字节的裸存储（对齐 8），可通过 placement 思路
// 存放任意不超过该尺寸的类型（教学版直接存 int 演示）。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace dt {

const int POOL_BLOCK = 64;      // 每节点字节数

struct PoolChunk {
    unsigned char data[POOL_BLOCK];
    PoolChunk* next;            // 空闲链表
};

class objpool {
public:
    explicit objpool(int capacity) : free_head_(0), total_(0), capacity_(capacity) {
        if (capacity_ < 1) capacity_ = 1;
        for (int i = 0; i < capacity_; i++) {
            PoolChunk* c = new PoolChunk;
            c->next = free_head_;
            free_head_ = c;
        }
        total_ = capacity_;
    }
    ~objpool() {
        // 释放所有 chunk（含在使用中的：教学版统一回收）
        PoolChunk* c = free_head_;
        while (c) { PoolChunk* nx = c->next; delete c; c = nx; }
    }
    objpool(const objpool&) = delete;
    objpool& operator=(const objpool&) = delete;

    // 取出一个节点；池空返回 0。返回裸指针（可放入任意 <=64B 数据）
    void* alloc_ptr() {
        if (!free_head_) return 0;
        PoolChunk* c = free_head_;
        free_head_ = c->next;
        used_++;
        return (void*)c->data;
    }
    // 归还一个节点
    void free_ptr(void* p) {
        // 从 data 反推 chunk（data 是 chunk 第一个成员）
        PoolChunk* c = (PoolChunk*)p;
        c->next = free_head_;
        free_head_ = c;
        used_--;
    }
    int free_count() const {
        int n = 0;
        PoolChunk* c = free_head_;
        while (c) { n++; c = c->next; }
        return n;
    }
    int used_count() const { return used_; }
    int capacity() const { return capacity_; }

private:
    PoolChunk* free_head_;
    int total_;
    int used_;
    int capacity_;
};

int objpool_self_test();

} // namespace dt
} // namespace nefu
