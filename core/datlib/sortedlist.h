// nefuOS data-types library — sorted list (sortedlist)
// 有序列表：元素始终按序存储，支持插入保持有序、按排名访问、
// 按值查排名。实现为"分块跳表"风格的简单多级链表——
// 教学版采用跳表（skiplist）：随机层级 + 指针跳过，期望 O(log n)
// 查找/插入/删除，实现比红黑树简单、无旋转。
// 支持：插入、删除、查找、第 k 小、排名、升序转储。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace dt {

const int SL_MAX_LEVEL = 16;

struct SLNode {
    int val;
    int count;          // 该值出现次数（重复合并）
    int level;
    SLNode** next;      // next[i] = 第 i 层后继
};

class sortedlist {
public:
    sortedlist() : head_(0), size_(0), rng_(0xDEADBEEFu) {
        head_ = new SLNode;
        head_->val = 0; head_->count = 0; head_->level = SL_MAX_LEVEL;
        head_->next = new SLNode*[SL_MAX_LEVEL];
        for (int i = 0; i < SL_MAX_LEVEL; i++) head_->next[i] = 0;
    }
    ~sortedlist() {
        SLNode* n = head_->next[0];
        while (n) { SLNode* nx = n->next[0]; delete[] n->next; delete n; n = nx; }
        delete[] head_->next;
        delete head_;
    }
    sortedlist(const sortedlist&) = delete;
    sortedlist& operator=(const sortedlist&) = delete;

    int size() const { return size_; }          // 不同值的个数
    int total() const {                        // 全部元素个数（含重复）
        int s = 0;
        SLNode* n = head_->next[0];
        while (n) { s += n->count; n = n->next[0]; }
        return s;
    }
    bool empty() const { return size_ == 0; }

    // 插入一个值（重复则计数 +1）
    void insert(int v) {
        SLNode* pred[SL_MAX_LEVEL];
        SLNode* cur = head_;
        for (int i = SL_MAX_LEVEL - 1; i >= 0; i--) {
            while (cur->next[i] && cur->next[i]->val < v) cur = cur->next[i];
            pred[i] = cur;
        }
        cur = cur->next[0];
        if (cur && cur->val == v) { cur->count++; return; }   // 重复
        // 随机层级
        int lvl = 1;
        while (lvl < SL_MAX_LEVEL && (next_rand() & 3) == 0) lvl++;
        SLNode* nn = new SLNode;
        nn->val = v; nn->count = 1; nn->level = lvl;
        nn->next = new SLNode*[lvl];
        for (int i = 0; i < lvl; i++) {
            nn->next[i] = pred[i]->next[i];
            pred[i]->next[i] = nn;
        }
        size_++;
    }
    // 删除一个值（计数 -1；归零删除节点）；返回是否删除
    bool remove(int v) {
        SLNode* pred[SL_MAX_LEVEL];
        SLNode* cur = head_;
        for (int i = SL_MAX_LEVEL - 1; i >= 0; i--) {
            while (cur->next[i] && cur->next[i]->val < v) cur = cur->next[i];
            pred[i] = cur;
        }
        cur = cur->next[0];
        if (!cur || cur->val != v) return false;
        if (--cur->count > 0) return true;
        for (int i = 0; i < cur->level; i++) pred[i]->next[i] = cur->next[i];
        delete[] cur->next;
        delete cur;
        size_--;
        return true;
    }
    bool contains(int v) const {
        SLNode* cur = head_;
        for (int i = SL_MAX_LEVEL - 1; i >= 0; i--) {
            while (cur->next[i] && cur->next[i]->val < v) cur = cur->next[i];
        }
        cur = cur->next[0];
        return cur && cur->val == v;
    }
    // 第 k 小（k 从 0 起，按计数展开）；不存在返回 false
    bool kth(int k, int& val) const {
        int s = 0;
        SLNode* n = head_->next[0];
        while (n) {
            if (k < s + n->count) { val = n->val; return true; }
            s += n->count;
            n = n->next[0];
        }
        return false;
    }
    // 小于 v 的元素个数
    int count_less(int v) const {
        int s = 0;
        SLNode* n = head_->next[0];
        while (n && n->val < v) { s += n->count; n = n->next[0]; }
        return s;
    }
    // 升序转储（去重值）；返回个数
    int dump(int* out, int cap) const {
        int k = 0;
        SLNode* n = head_->next[0];
        while (n && k < cap) { out[k++] = n->val; n = n->next[0]; }
        return k;
    }
    // 中位数（偶数取较小的那个）
    bool median(int& m) const {
        int t = total();
        if (t == 0) return false;
        return kth(t / 2, m);
    }

private:
    SLNode* head_;
    int size_;
    unsigned rng_;

    unsigned next_rand() {
        unsigned x = rng_;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        rng_ = x;
        return x;
    }
};

int sortedlist_self_test();

} // namespace dt
} // namespace nefu
