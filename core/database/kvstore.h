// nefuOS 嵌入式数据库 —— 键值存储(KV Store)
//
// 两层结构:
//   1) 内存哈希表(拉链法)负责 O(1) 精确点查 —— 这是热路径。
//   2) Write-Ahead Log(WAL)负责持久性 —— 每次修改先写日志,再改内存。
//
// 崩溃恢复:
//   打开一个新的 KVStore 时,先建空表,然后重放整个 WAL:
//   看到 PUT 就插入,看到 DEL 就删除。重放完毕即恢复到崩溃前状态。
//
// 批量写(batch):
//   一次批量写在外层包一对 BEGIN/COMMIT 日志记录;批量内的修改先攒着,
//   commit 时才一次性写日志并生效。这里给出一个简化但可用的实现。
//
// 快照(snapshot):
//   把当前所有键值对序列化到一块字节缓冲,等价于一个检查点。
//   之后 WAL 里 seq <= 快照点的记录都可以 truncate。
#pragma once
#include "../klib/klib.h"
#include "wal.h"

namespace nefu {
namespace database {

// ---- 哈希表桶里的一条记录 ----
struct KVEntry {
    String   key;
    String   value;
    KVEntry* next;
};

// ---- 键值存储 ----
class KVStore {
public:
    // nbuckets:哈希桶数量(建议取素数);0 用默认值。
    KVStore(int nbuckets = 0);
    ~KVStore();

    // 点查:存在返回 true 并把值写入 out(可为空)。
    bool get(const String& key, String* out_value) const;
    bool exists(const String& key) const { return get(key, 0); }

    // 写入 / 覆盖。会先写 WAL 再改内存。
    void put(const String& key, const String& value);

    // 删除。成功返回 true。
    bool del(const String& key);

    // 键的总数
    int size() const { return count_; }

    // 顺序遍历所有键(不保证顺序)。callback 可改空指针。
    typedef void (*IterFn)(void* ctx, const String& key, const String& value);
    void iterate(IterFn fn, void* ctx) const;

    // ---- 批量写 ----
    // 简化事务:批量期间 put/del 先记入临时区,commit() 时一次性写 WAL 并生效。
    void batch_begin();
    void batch_put(const String& key, const String& value);
    void batch_del(const String& key);
    void batch_commit();      // 真正落盘 + 生效
    void batch_abort();       // 丢弃批量修改

    // ---- 快照 / 恢复 ----
    // 导出全量快照(紧凑二进制),调用者 delete[] 释放,out_len 输出长度。
    uint8_t* snapshot(int* out_len) const;
    // 从快照重建(会先清空),再叠加重放后续 WAL。返回恢复出的键数。
    int restore_from_snapshot(const uint8_t* data, int len);

    // 用一段日志字节模拟 "崩溃后从磁盘读回 WAL 并重放恢复"。
    // 返回重放的操作条数。
    int recover_from_wal(const uint8_t* wal_data, int wal_len);

    // 直接暴露内部 WAL(供外部做检查点)
    Wal& wal() { return wal_; }

    // 清空全部数据(含日志)
    void clear();

    friend void kv_replay_visitor(void*, uint8_t, const char*, int, const char*, int, uint32_t);

private:
    // 哈希函数(FNV-1a)
    static uint32_t hash_of(const String& key);
    // 在桶链里找一条,返回其指针与前驱
    KVEntry* find_entry(const String& key, KVEntry** prev_out = 0) const;
    // 把内存里真正写入 / 删除(不写 WAL)—— 供重放用
    void apply_put(const String& key, const String& value);
    void apply_del(const String& key);

    int       nbuckets_;
    KVEntry** buckets_;
    int       count_;
    Wal       wal_;

    // 批量临时区(用两个 List 模拟)
    bool            batch_on_;
    List<String>    batch_keys_;
    List<String>    batch_vals_;
    List<int>       batch_ops_;     // 0=put, 1=del
};

// 模块自检:返回失败断言数(0 表示全部通过)
int kvstore_self_test();

} // namespace database
} // namespace nefu
