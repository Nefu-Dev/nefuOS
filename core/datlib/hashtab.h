// nefuOS data-types library — open-addressing hash table (hashtab)
// 开放寻址哈希表：线性探测解决冲突，泛型键值对（int 键 -> 泛型值）。
// 与链地址法（algo/ds 的 IntHashMap）不同，元素直接存在桶数组里，
// 无指针开销、缓存友好；代价是删除要打"墓碑"标记（TOMBSTONE）。
// 负载因子超 0.7 自动扩容翻倍重哈希。适用于查找密集型小数据。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace dt {

// 值类型默认为 int；模板参数 V 可换成任何可拷贝类型
template <typename V>
class hashtab {
public:
    enum SlotState { SLOT_EMPTY = 0, SLOT_LIVE, SLOT_TOMB };

    struct Entry {
        int   key;
        V     val;
        int   state;
    };

    explicit hashtab(int cap = 16) : entries_(0), cap_(0), size_(0), tombstones_(0) {
        rehash_table(cap < 8 ? 8 : cap);
    }
    ~hashtab() { if (entries_) { delete[] entries_; entries_ = 0; } }
    hashtab(const hashtab&) = delete;
    hashtab& operator=(const hashtab&) = delete;

    int size() const { return size_; }
    bool empty() const { return size_ == 0; }

    // 插入/更新；返回 true 表示新键，false 表示覆盖旧值
    bool put(int key, const V& val) {
        if ((size_ + tombstones_) * 10 >= cap_ * 7) rehash_table(cap_ * 2);
        int i = find_slot(key);
        if (entries_[i].state == SLOT_LIVE) {
            entries_[i].val = val;           // 已存在：更新
            return false;
        }
        entries_[i].key = key;
        entries_[i].val = val;
        entries_[i].state = SLOT_LIVE;
        size_++;
        return true;
    }

    // 查询；找到返回 true 并写 val
    bool get(int key, V& val) const {
        int i = probe(key);
        if (i >= 0 && entries_[i].state == SLOT_LIVE) { val = entries_[i].val; return true; }
        return false;
    }
    bool contains(int key) const { return probe(key) >= 0; }

    // 删除（打墓碑）；返回是否删除成功
    bool remove(int key) {
        int i = probe(key);
        if (i < 0) return false;
        entries_[i].state = SLOT_TOMB;
        size_--;
        tombstones_++;
        return true;
    }
    void clear() {
        for (int i = 0; i < cap_; i++) entries_[i].state = SLOT_EMPTY;
        size_ = 0; tombstones_ = 0;
    }

    // 迭代（返回下一个 LIVE 下标；从 idx=0 开始循环）--------------
    // 用法：for (int i = h.first_live(); i >= 0; i = h.next_live(i)) ...
    int first_live() const { return next_live(-1); }
    int next_live(int prev) const {
        for (int i = prev + 1; i < cap_; i++) if (entries_[i].state == SLOT_LIVE) return i;
        return -1;
    }
    int key_at(int i) const { return entries_[i].key; }
    V&   val_at(int i) { return entries_[i].val; }
    const V& val_at(int i) const { return entries_[i].val; }

    // 当前桶容量
    int capacity() const { return cap_; }

private:
    static unsigned hash_key(int key) {
        // 乘法散列 + 搅动：避免小整数直接映射
        unsigned h = (unsigned)key;
        h = (h ^ (h >> 16)) * 0x45d9f3b;
        h = (h ^ (h >> 16)) * 0x45d9f3b;
        h ^= h >> 16;
        return h;
    }
    // 线性探测：返回 LIVE 的 key 下标，或首个可插入位置（EMPTY/TOMB）
    int find_slot(int key) {
        unsigned h = hash_key(key) % (unsigned)cap_;
        for (int step = 0; step < cap_; step++) {
            int i = (int)((h + step) % (unsigned)cap_);
            if (entries_[i].state == SLOT_EMPTY || entries_[i].state == SLOT_TOMB) return i;
            if (entries_[i].state == SLOT_LIVE && entries_[i].key == key) return i;
        }
        return 0;   // 表已满（理论上不会，扩容保证）
    }
    // 只查已存在的键：命中 LIVE 返回下标，否则 -1
    int probe(int key) const {
        unsigned h = hash_key(key) % (unsigned)cap_;
        for (int step = 0; step < cap_; step++) {
            int i = (int)((h + step) % (unsigned)cap_);
            if (entries_[i].state == SLOT_EMPTY) return -1;   // 空槽可停
            if (entries_[i].state == SLOT_LIVE && entries_[i].key == key) return i;
        }
        return -1;
    }
    void rehash_table(int nc) {
        Entry* old = entries_;
        int oc = cap_;
        entries_ = new Entry[nc];
        cap_ = nc;
        for (int i = 0; i < cap_; i++) entries_[i].state = SLOT_EMPTY;
        size_ = 0; tombstones_ = 0;
        if (old) {
            for (int i = 0; i < oc; i++) {
                if (old[i].state == SLOT_LIVE) put_norehash(old[i].key, old[i].val);
            }
            delete[] old;
        }
    }
    void put_norehash(int key, const V& val) {
        int i = find_slot(key);
        entries_[i].key = key;
        entries_[i].val = val;
        entries_[i].state = SLOT_LIVE;
        size_++;
    }

    Entry* entries_;
    int    cap_;
    int    size_;
    int    tombstones_;
};

int hashtab_self_test();

} // namespace dt
} // namespace nefu
