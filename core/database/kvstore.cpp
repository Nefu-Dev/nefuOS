// nefuOS 嵌入式数据库 —— 键值存储实现
#include "kvstore.h"

namespace nefu {
namespace database {

// ===================== 构造 / 析构 =====================
KVStore::KVStore(int nbuckets) : count_(0), batch_on_(false) {
    // 取一个素数桶数,减少哈希冲突
    static const int primes[] = { 17, 31, 61, 127, 251, 509, 1021 };
    nbuckets_ = 17;
    for (int i = 0; i < (int)(sizeof(primes) / sizeof(primes[0])); i++) {
        if (primes[i] >= nbuckets) { nbuckets_ = primes[i]; break; }
    }
    if (nbuckets <= 0) nbuckets_ = 61;
    buckets_ = new KVEntry*[nbuckets_];
    for (int i = 0; i < nbuckets_; i++) buckets_[i] = 0;
}
KVStore::~KVStore() { clear(); }

void KVStore::clear() {
    for (int i = 0; i < nbuckets_; i++) {
        KVEntry* e = buckets_[i];
        while (e) {
            KVEntry* n = e->next;
            delete e;
            e = n;
        }
        buckets_[i] = 0;
    }
    count_ = 0;
    wal_.truncate_all();
    batch_on_ = false;
    batch_keys_.clear();
    batch_vals_.clear();
    batch_ops_.clear();
}

// ===================== 哈希 =====================
uint32_t KVStore::hash_of(const String& key) {
    uint32_t h = 2166136261u;   // FNV offset basis
    const char* p = key.c_str();
    for (int i = 0; i < key.len(); i++) {
        h ^= (uint8_t)p[i];
        h *= 16777619u;         // FNV prime
    }
    return h;
}

KVEntry* KVStore::find_entry(const String& key, KVEntry** prev_out) const {
    uint32_t b = hash_of(key) % (uint32_t)nbuckets_;
    KVEntry* e = buckets_[b];
    KVEntry* prev = 0;
    while (e) {
        if (e->key == key) {
            if (prev_out) *prev_out = prev;
            return e;
        }
        prev = e;
        e = e->next;
    }
    if (prev_out) *prev_out = 0;
    return 0;
}

// ===================== 内存操作(不写日志) =====================
void KVStore::apply_put(const String& key, const String& value) {
    KVEntry* prev = 0;
    KVEntry* e = find_entry(key, &prev);
    if (e) {
        e->value = value;          // 覆盖
        return;
    }
    // 新建,插到桶头
    uint32_t b = hash_of(key) % (uint32_t)nbuckets_;
    KVEntry* ne = new KVEntry();
    ne->key = key;
    ne->value = value;
    ne->next = buckets_[b];
    buckets_[b] = ne;
    count_++;
}

void KVStore::apply_del(const String& key) {
    KVEntry* prev = 0;
    KVEntry* e = find_entry(key, &prev);
    if (!e) return;
    if (prev) prev->next = e->next;
    else      buckets_[hash_of(key) % (uint32_t)nbuckets_] = e->next;
    delete e;
    count_--;
}

// ===================== 点查 =====================
bool KVStore::get(const String& key, String* out_value) const {
    KVEntry* e = find_entry(key);
    if (!e) return false;
    if (out_value) *out_value = e->value;
    return true;
}

// ===================== 写 / 删(带 WAL) =====================
void KVStore::put(const String& key, const String& value) {
    if (batch_on_) {
        batch_keys_.push(key);
        batch_vals_.push(value);
        batch_ops_.push(0);     // put
        return;
    }
    wal_.append_str(WAL_PUT, key.c_str(), value.c_str(), wal_.next_seq());
    apply_put(key, value);
}

bool KVStore::del(const String& key) {
    if (!exists(key)) return false;
    if (batch_on_) {
        batch_keys_.push(key);
        batch_vals_.push(String(""));
        batch_ops_.push(1);     // del
        return true;
    }
    wal_.append_str(WAL_DEL, key.c_str(), 0, wal_.next_seq());
    apply_del(key);
    return true;
}

// ===================== 遍历 =====================
void KVStore::iterate(IterFn fn, void* ctx) const {
    if (!fn) return;
    for (int i = 0; i < nbuckets_; i++) {
        for (KVEntry* e = buckets_[i]; e; e = e->next) {
            fn(ctx, e->key, e->value);
        }
    }
}

// ===================== 批量写 =====================
void KVStore::batch_begin() {
    batch_on_ = true;
    batch_keys_.clear();
    batch_vals_.clear();
    batch_ops_.clear();
}

void KVStore::batch_put(const String& key, const String& value) {
    if (!batch_on_) { put(key, value); return; }
    batch_keys_.push(key);
    batch_vals_.push(value);
    batch_ops_.push(0);
}

void KVStore::batch_del(const String& key) {
    if (!batch_on_) { del(key); return; }
    batch_keys_.push(key);
    batch_vals_.push(String(""));
    batch_ops_.push(1);
}

void KVStore::batch_commit() {
    if (!batch_on_) return;
    // 写一对事务边界,再把每条修改写日志并生效
    wal_.append_str(WAL_BEGIN, "", "", wal_.next_seq());
    int n = batch_keys_.size();
    for (int i = 0; i < n; i++) {
        const String& k = batch_keys_[i];
        const String& v = batch_vals_[i];
        int op = batch_ops_[i];
        if (op == 0) {
            wal_.append_str(WAL_PUT, k.c_str(), v.c_str(), wal_.next_seq());
            apply_put(k, v);
        } else {
            wal_.append_str(WAL_DEL, k.c_str(), 0, wal_.next_seq());
            apply_del(k);
        }
    }
    wal_.append_str(WAL_COMMIT, "", "", wal_.next_seq());
    batch_on_ = false;
    batch_keys_.clear();
    batch_vals_.clear();
    batch_ops_.clear();
}

void KVStore::batch_abort() {
    batch_on_ = false;
    batch_keys_.clear();
    batch_vals_.clear();
    batch_ops_.clear();
}

// ===================== 快照 / 恢复 =====================
uint8_t* KVStore::snapshot(int* out_len) const {
    // 格式:[magic 4B][count u32] 重复 [klen u16][key][vlen u16][value]
    int total = 8;
    for (int i = 0; i < nbuckets_; i++)
        for (KVEntry* e = buckets_[i]; e; e = e->next)
            total += 2 + e->key.len() + 2 + e->value.len();
    uint8_t* out = new uint8_t[total > 8 ? total : 9];
    out[0] = 'K'; out[1] = 'V'; out[2] = 'S'; out[3] = 1;
    int c = count_;
    out[4] = (uint8_t)c; out[5] = (uint8_t)(c >> 8);
    out[6] = (uint8_t)(c >> 16); out[7] = (uint8_t)(c >> 24);
    int p = 8;
    for (int i = 0; i < nbuckets_; i++) {
        for (KVEntry* e = buckets_[i]; e; e = e->next) {
            int kl = e->key.len(), vl = e->value.len();
            out[p++] = (uint8_t)kl; out[p++] = (uint8_t)(kl >> 8);
            for (int j = 0; j < kl; j++) out[p++] = (uint8_t)e->key[j];
            out[p++] = (uint8_t)vl; out[p++] = (uint8_t)(vl >> 8);
            for (int j = 0; j < vl; j++) out[p++] = (uint8_t)e->value[j];
        }
    }
    if (out_len) *out_len = p;
    return out;
}

int KVStore::restore_from_snapshot(const uint8_t* data, int len) {
    // 先清空内存(保留 WAL 对象本身)
    for (int i = 0; i < nbuckets_; i++) {
        KVEntry* e = buckets_[i];
        while (e) { KVEntry* n = e->next; delete e; e = n; }
        buckets_[i] = 0;
    }
    count_ = 0;
    if (!data || len < 8) return 0;
    if (data[0] != 'K' || data[1] != 'V' || data[2] != 'S') return 0;
    int cnt = (int)data[4] | ((int)data[5] << 8) | ((int)data[6] << 16) | ((int)data[7] << 24);
    int p = 8;
    int got = 0;
    for (int i = 0; i < cnt && p + 2 <= len; i++) {
        int kl = data[p] | (data[p + 1] << 8); p += 2;
        if (p + kl > len) break;
        String k((const char*)(data + p), kl); p += kl;
        if (p + 2 > len) break;
        int vl = data[p] | (data[p + 1] << 8); p += 2;
        if (p + vl > len) break;
        String v((const char*)(data + p), vl); p += vl;
        apply_put(k, v);
        got++;
    }
    return got;
}

// 重放 WAL 的 visitor:把操作应用到空表上
void kv_replay_visitor(void* ctx, uint8_t op,
                              const char* key, int klen,
                              const char* value, int vlen, uint32_t) {
    KVStore* kv = (KVStore*)ctx;
    if (op == WAL_PUT) {
        kv->apply_put(String(key, klen), String(value, vlen));
    } else if (op == WAL_DEL) {
        kv->apply_del(String(key, klen));
    }
}

int KVStore::recover_from_wal(const uint8_t* wal_data, int wal_len) {
    // 清空内存,导入日志,重放
    for (int i = 0; i < nbuckets_; i++) {
        KVEntry* e = buckets_[i];
        while (e) { KVEntry* n = e->next; delete e; e = n; }
        buckets_[i] = 0;
    }
    count_ = 0;
    wal_.truncate_all();
    wal_.import_bytes(wal_data, wal_len);
    return wal_.replay(kv_replay_visitor, this);
}

// ===================== 自检 =====================
int kvstore_self_test() {
    int fail = 0;

    // 1) 基本 put/get/del/exists
    {
        KVStore kv;
        kv.put(String("a"), String("1"));
        String v;
        if (!kv.get(String("a"), &v) || strcmp(v.c_str(), "1") != 0) fail++;
        if (!kv.exists(String("a"))) fail++;
        if (kv.exists(String("nope"))) fail++;

        kv.put(String("a"), String("2"));          // 覆盖
        if (!kv.get(String("a"), &v) || strcmp(v.c_str(), "2") != 0) fail++;
        if (kv.size() != 1) fail++;

        if (!kv.del(String("a"))) fail++;
        if (kv.exists(String("a"))) fail++;
        if (kv.del(String("a"))) fail++;           // 再删应失败
    }

    // 2) 大批量写入 + 查找
    {
        KVStore kv;
        for (int i = 0; i < 500; i++) {
            char k[16]; ksprintf(k, sizeof(k), "user%d", i);
            char v[16]; ksprintf(v, sizeof(v), "name%d", i);
            kv.put(String(k), String(v));
        }
        if (kv.size() != 500) fail++;
        for (int i = 0; i < 500; i += 7) {
            char k[16]; ksprintf(k, sizeof(k), "user%d", i);
            char want[16]; ksprintf(want, sizeof(want), "name%d", i);
            String got;
            if (!kv.get(String(k), &got) || strcmp(got.c_str(), want) != 0) fail++;
        }
    }

    // 3) 崩溃恢复:写一批 → 导出 WAL → 新 KVStore 从该 WAL 恢复
    {
        KVStore kv;
        kv.put(String("x"), String("100"));
        kv.put(String("y"), String("200"));
        kv.put(String("z"), String("300"));
        kv.del(String("y"));
        int wlen = 0;
        uint8_t* walblob = kv.wal().export_bytes(&wlen);

        KVStore recovered;
        int n = recovered.recover_from_wal(walblob, wlen);
        delete[] walblob;
        if (n != 4) fail++;                         // PUT x,PUT y,PUT z,DEL y
        if (recovered.size() != 2) fail++;          // x,z
        String v;
        if (!recovered.get(String("x"), &v) || strcmp(v.c_str(), "100") != 0) fail++;
        if (!recovered.get(String("z"), &v) || strcmp(v.c_str(), "300") != 0) fail++;
        if (recovered.exists(String("y"))) fail++;  // y 应被 DEL 重放删掉
    }

    // 4) 快照 + 检查点:快照后 truncate WAL,再恢复仍正确
    {
        KVStore kv;
        kv.put(String("a"), String("1"));
        kv.put(String("b"), String("2"));
        int snaplen = 0;
        uint8_t* snap = kv.snapshot(&snaplen);
        // 做检查点:假设快照已含全部,截断 WAL
        kv.wal().checkpoint(0xFFFFFFFFu);

        KVStore kv2;
        int got = kv2.restore_from_snapshot(snap, snaplen);
        delete[] snap;
        if (got != 2) fail++;
        if (!kv2.exists(String("a")) || !kv2.exists(String("b"))) fail++;
    }

    // 5) 批量写:commit 生效,abort 丢弃
    {
        KVStore kv;
        kv.put(String("base"), String("0"));
        kv.batch_begin();
        kv.batch_put(String("b1"), String("1"));
        kv.batch_put(String("b2"), String("2"));
        kv.batch_del(String("base"));
        // commit 前不应生效
        if (kv.exists(String("b1"))) fail++;
        kv.batch_commit();
        if (!kv.exists(String("b1")) || !kv.exists(String("b2"))) fail++;
        if (kv.exists(String("base"))) fail++;      // base 被批量删了

        // abort 路径
        kv.batch_begin();
        kv.batch_put(String("ghost"), String("x"));
        kv.batch_abort();
        if (kv.exists(String("ghost"))) fail++;
    }

    // 6) 遍历计数正确
    {
        KVStore kv;
        for (int i = 0; i < 20; i++) {
            char k[8]; ksprintf(k, sizeof(k), "e%d", i);
            kv.put(String(k), String(k));
        }
        int seen = 0;
        KVStore::IterFn fn = [](void* ctx, const String&, const String&) {
            (*(int*)ctx)++;
        };
        kv.iterate(fn, &seen);
        if (seen != 20) fail++;
    }

    return fail;
}

} // namespace database
} // namespace nefu
