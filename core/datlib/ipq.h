// nefuOS data-types library — indexed priority queue (ipq)
// 索引优先队列：二叉堆 + 位置索引表。支持 O(log n) 的 decrease-key
// 与 O(1) 的 contains/peek，是 Dijkstra、Prim、A* 等算法的关键组件
// （普通优先队列无法在堆内快速修改任意元素）。
// 接口：push(id, prio) / pop_min(id&, prio&) / decrease_key(id, prio) /
// contains(id) / priority(id)。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace dt {

const int IPQ_MAX = 512;

class ipq {
public:
    ipq() : size_(0) {
        for (int i = 0; i < IPQ_MAX; i++) {
            pos_[i] = -1;      // -1 = 不在堆中
            prio_[i] = 0;
        }
    }
    // 把 id 以 prio 插入；id 已存在则忽略（用 decrease_key 更新）
    void push(int id, int prio) {
        if (id < 0 || id >= IPQ_MAX || contains(id)) return;
        prio_[id] = prio;
        pos_[id] = size_;
        heap_[size_] = id;
        size_++;
        sift_up(size_ - 1);
    }
    // 弹出最小元素；空返回 false
    bool pop_min(int& id, int& prio) {
        if (size_ == 0) return false;
        id = heap_[0];
        prio = prio_[id];
        pos_[id] = -1;
        size_--;
        if (size_ > 0) {
            heap_[0] = heap_[size_];
            pos_[heap_[0]] = 0;
            sift_down(0);
        }
        return true;
    }
    // 降低 id 的优先级（新值必须更小）；不存在或更大则忽略
    void decrease_key(int id, int prio) {
        if (!contains(id) || prio >= prio_[id]) return;
        prio_[id] = prio;
        sift_up(pos_[id]);
    }
    bool contains(int id) const { return id >= 0 && id < IPQ_MAX && pos_[id] >= 0; }
    bool empty() const { return size_ == 0; }
    int size() const { return size_; }
    // 当前优先级；不存在返回 0
    int priority(int id) const { return contains(id) ? prio_[id] : 0; }

private:
    int heap_[IPQ_MAX];     // 堆数组：存 id
    int pos_[IPQ_MAX];      // id -> 堆下标
    int prio_[IPQ_MAX];     // id -> 优先级
    int size_;

    void sift_up(int i) {
        while (i > 0) {
            int p = (i - 1) / 2;
            if (prio_[heap_[p]] <= prio_[heap_[i]]) break;
            swap_idx(i, p);
            i = p;
        }
    }
    void sift_down(int i) {
        for (;;) {
            int l = i * 2 + 1, r = i * 2 + 2, m = i;
            if (l < size_ && prio_[heap_[l]] < prio_[heap_[m]]) m = l;
            if (r < size_ && prio_[heap_[r]] < prio_[heap_[m]]) m = r;
            if (m == i) break;
            swap_idx(i, m);
            i = m;
        }
    }
    void swap_idx(int a, int b) {
        int t = heap_[a]; heap_[a] = heap_[b]; heap_[b] = t;
        pos_[heap_[a]] = a;
        pos_[heap_[b]] = b;
    }
};

int ipq_self_test();

} // namespace dt
} // namespace nefu
