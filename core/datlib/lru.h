// nefuOS data-types library — LRU cache (lru)
// LRU 缓存：最近最少使用淘汰。哈希表 O(1) 定位 + 双向链表 O(1)
// 维护访问顺序。命中时把节点移到链表头；淘汰时删链表尾。
// 经典"哈希 + 双向链表"组合，用于缓存、页面置换等。
// 本实现键为 int，值为 int；容量构造时固定。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace dt {

struct LRUNode {
    int key;
    int val;
    LRUNode* prev;
    LRUNode* next;      // 双向链表指针
    LRUNode* hash_next; // 哈希冲突链（与链表分开，避免互相覆盖）
};

class lru {
public:
    explicit lru(int cap) : cap_(cap), head_(0), tail_(0), count_(0) {
        if (cap_ < 1) cap_ = 1;
        // 哈希表桶
        buckets_ = new LRUNode*[cap_ * 2];
        nbuckets_ = cap_ * 2;
        for (int i = 0; i < nbuckets_; i++) buckets_[i] = 0;
    }
    ~lru() {
        LRUNode* n = head_;
        while (n) { LRUNode* nx = n->next; delete n; n = nx; }
        if (buckets_) { delete[] buckets_; buckets_ = 0; }
    }
    lru(const lru&) = delete;
    lru& operator=(const lru&) = delete;

    // 查询：命中返回 true 并写 val（同时提升为最近使用）
    bool get(int key, int& val) {
        LRUNode* n = find(key);
        if (!n) return false;
        val = n->val;
        move_to_head(n);
        return true;
    }
    // 插入/更新；返回是否发生淘汰（被逐出的键写入 evicted）
    bool put(int key, int val, int& evicted_key) {
        LRUNode* n = find(key);
        if (n) {
            n->val = val;
            move_to_head(n);
            return false;
        }
        bool evicted = false;
        if (count_ >= cap_) {
            evicted_key = tail_->key;
            hash_remove(evicted_key);   // 先摘哈希链（节点仍有效）
            unlink(tail_);              // 再删链表节点
            count_--;
            evicted = true;
        }
        n = new LRUNode;
        n->key = key; n->val = val; n->prev = 0; n->next = 0;
        link_head(n);
        hash_insert(n);
        count_++;
        return evicted;
    }
    bool contains(int key) const { return find(key) != 0; }
    int size() const { return count_; }
    int capacity() const { return cap_; }
    // 最近使用顺序转储（头 = 最近）
    int dump(int* keys, int* vals, int n) const {
        int k = 0;
        LRUNode* cur = head_;
        while (cur && k < n) { keys[k] = cur->key; vals[k] = cur->val; k++; cur = cur->next; }
        return k;
    }
private:
    int cap_;
    LRUNode* head_;
    LRUNode* tail_;
    int count_;
    LRUNode** buckets_;
    int nbuckets_;

    static unsigned hash_key(int key) {
        unsigned h = (unsigned)key;
        h ^= h >> 16; h *= 0x7feb352d;
        h ^= h >> 15; h *= 0x846ca68b;
        h ^= h >> 16;
        return h;
    }
    LRUNode* find(int key) const {
        unsigned h = hash_key(key) % (unsigned)nbuckets_;
        LRUNode* n = buckets_[h];
        while (n) { if (n->key == key) return n; n = n->hash_next; }
        return 0;
    }
    void hash_insert(LRUNode* n) {
        unsigned h = hash_key(n->key) % (unsigned)nbuckets_;
        n->hash_next = buckets_[h];   // 头插（独立哈希链）
        buckets_[h] = n;
    }
    void hash_remove(int key) {
        unsigned h = hash_key(key) % (unsigned)nbuckets_;
        LRUNode** p = &buckets_[h];
        while (*p) {
            if ((*p)->key == key) { *p = (*p)->hash_next; return; }
            p = &(*p)->hash_next;
        }
    }
    void link_head(LRUNode* n) {
        n->prev = 0; n->next = head_;
        if (head_) head_->prev = n;
        head_ = n;
        if (!tail_) tail_ = n;
    }
    void unlink(LRUNode* n) {
        if (n->prev) n->prev->next = n->next; else head_ = n->next;
        if (n->next) n->next->prev = n->prev; else tail_ = n->prev;
        delete n;
    }
    void move_to_head(LRUNode* n) {
        if (n == head_) return;
        if (n->prev) n->prev->next = n->next;
        if (n->next) n->next->prev = n->prev;
        if (n == tail_) tail_ = n->prev;
        n->prev = 0; n->next = head_;
        if (head_) head_->prev = n;
        head_ = n;
    }
};

int lru_self_test();

} // namespace dt
} // namespace nefu
