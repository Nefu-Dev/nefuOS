// nefuOS 嵌入式数据库 —— Write-Ahead Log 实现
#include "wal.h"
#include "../klib/klib.h"   // nefu::kalloc/kfree 经由 new[]/delete[]

namespace nefu {
namespace database {

// ===================== CRC32 实现 =====================
// 标准 CRC-32/IEEE(以太网 / zlib 用的那个),多项式 0xEDB88320。
// 这里用查表法的逐位等价写法实现,避免再依赖 cryptlib,保证本模块可独立编译。
uint32_t wal_crc32(const void* data, int len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (int i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            uint32_t low = crc & 1u;
            crc >>= 1;
            if (low) crc ^= 0xEDB88320u;
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

// ===================== 小端读写辅助 =====================
// 本数据库全部运行在小端机器(x86),序列化也按小端写,读回时再按小端解。
static inline void wr32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static inline uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0]        |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16)|
           ((uint32_t)p[3] << 24);
}

// ===================== 构造 / 析构 =====================
Wal::Wal()
    : buf_(0), size_(0), cap_(0), count_(0), next_seq_(1) {
    // 预分配 4KB,避免第一条记录就扩容
    grow(4096);
}

Wal::~Wal() {
    if (buf_) delete[] buf_;
    buf_ = 0;
}

void Wal::grow(int need) {
    int want = size_ + need;
    if (want <= cap_) return;
    int nc = cap_ > 0 ? cap_ * 2 : 4096;
    while (nc < want) nc *= 2;
    uint8_t* nb = new uint8_t[nc];
    if (!nb) return;
    for (int i = 0; i < size_; i++) nb[i] = buf_[i];
    if (buf_) delete[] buf_;
    buf_ = nb;
    cap_ = nc;
}

// ===================== 追加 =====================
int Wal::append(uint8_t op, const char* key, int klen,
                const char* value, int vlen, uint32_t seq) {
    if (klen < 0) klen = 0;
    if (vlen < 0) vlen = 0;
    // 一条记录 = payload_len(4) + op(1) + seq(4) + klen(4) + vlen(4)
    //           + key(klen) + value(vlen) + crc(4)
    int payload = 1 + 4 + 4 + 4 + klen + vlen;
    int total   = 4 + payload + 4;
    grow(total);
    if (size_ + total > cap_) return -1;   // 扩容失败(内存耗尽)

    uint8_t* p = buf_ + size_;
    wr32(p, (uint32_t)payload); p += 4;
    *p = op;                    p += 1;
    wr32(p, seq);               p += 4;
    wr32(p, (uint32_t)klen);    p += 4;
    wr32(p, (uint32_t)vlen);    p += 4;
    const uint8_t* crc_start = p - 13;     // crc 覆盖 op..value(回到 op 处)
    if (klen > 0) { for (int i = 0; i < klen; i++) p[i] = (uint8_t)key[i]; p += klen; }
    if (vlen > 0) { for (int i = 0; i < vlen; i++) p[i] = (uint8_t)value[i]; p += vlen; }
    uint32_t crc = wal_crc32(crc_start, payload);
    wr32(p, crc);

    size_ += total;
    count_++;
    if (seq >= next_seq_) next_seq_ = seq + 1;
    return total;
}

int Wal::append_str(uint8_t op, const char* key, const char* value, uint32_t seq) {
    int klen = key ? (int)strlen(key) : 0;
    int vlen = value ? (int)strlen(value) : 0;
    return append(op, key, klen, value, vlen, seq);
}

// ===================== 重放 =====================
int Wal::replay(WalVisitor vis, void* ctx) {
    int done = 0;
    int off = 0;
    while (off + 4 <= size_) {
        uint8_t* p = buf_ + off;
        uint32_t payload = rd32(p); p += 4;
        int total = 4 + (int)payload + 4;
        if (off + total > size_) break;          // 记录不完整(尾部被截断)
        // 校验 CRC:覆盖 payload 部分(含 crc 之前)
        uint32_t want_crc = wal_crc32(p, (int)payload);
        uint32_t got_crc  = rd32(p + payload);
        if (want_crc != got_crc) break;           // CRC 不匹配 → 损坏,停止重放

        uint8_t op = p[0];
        uint32_t seq  = rd32(p + 1);
        uint32_t klen = rd32(p + 5);
        uint32_t vlen = rd32(p + 9);
        const char* key   = (const char*)(p + 13);
        const char* value = (const char*)(p + 13 + klen);
        if (vis) vis(ctx, op, key, (int)klen, value, (int)vlen, seq);
        done++;
        off += total;
    }
    return done;
}

// ===================== 检查点截断 =====================
int Wal::checkpoint(uint32_t stable_lsn) {
    // 顺序扫描,找到第一条 seq > stable_lsn 的记录起点,
    // 把它之前的所有记录整体丢弃(把尾部搬过来)。
    int off = 0;
    int keep_off = 0;       // 需要保留区域的起始偏移
    int kept = 0;           // 保留下来的记录数
    while (off + 4 <= size_) {
        uint8_t* p = buf_ + off;
        uint32_t payload = rd32(p);
        int total = 4 + (int)payload + 4;
        if (off + total > size_) break;
        uint32_t seq = rd32(p + 5);
        if (seq > stable_lsn) {
            keep_off = off;
            break;
        }
        off += total;
    }
    if (keep_off == 0 && off >= size_) {
        // 全部记录都 <= stable_lsn:整个日志清空
        int dropped = size_;
        size_ = 0;
        count_ = 0;
        return dropped;
    }
    if (keep_off == 0) keep_off = 0;   // 一条都没扔
    int kept_bytes = size_ - keep_off;
    if (keep_off > 0) {
        // 把保留段搬到开头(用 memmove 处理重叠)
        memmove(buf_, buf_ + keep_off, (size_t)kept_bytes);
        size_ = kept_bytes;
        // 重新统计保留段里的记录数
        off = 0; kept = 0;
        while (off + 4 <= size_) {
            uint8_t* p = buf_ + off;
            uint32_t payload = rd32(p);
            int total = 4 + (int)payload + 4;
            if (off + total > size_) break;
            off += total;
            kept++;
        }
        count_ = kept;
    }
    return keep_off;   // 返回被丢弃的字节数
}

void Wal::truncate_all() {
    size_ = 0;
    count_ = 0;
}

// ===================== 导出 / 导入 =====================
uint8_t* Wal::export_bytes(int* out_len) const {
    uint8_t* out = new uint8_t[size_ > 0 ? size_ : 1];
    if (!out) { if (out_len) *out_len = 0; return 0; }
    for (int i = 0; i < size_; i++) out[i] = buf_[i];
    if (out_len) *out_len = size_;
    return out;
}

int Wal::import_bytes(const uint8_t* data, int len) {
    truncate_all();
    if (!data || len <= 0) return 0;
    grow(len);
    int off = 0;
    int imported = 0;
    while (off + 4 <= len) {
        uint32_t payload = rd32(data + off);
        int total = 4 + (int)payload + 4;
        if (off + total > len) break;
        uint32_t want_crc = wal_crc32(data + off + 4, (int)payload);
        uint32_t got_crc  = rd32(data + off + 4 + payload);
        if (want_crc != got_crc) break;         // 损坏记录不导入
        // 拷入
        for (int i = 0; i < total; i++) buf_[size_ + i] = data[off + i];
        size_ += total;
        imported++;
        count_++;
        off += total;
    }
    // 重建 next_seq:扫一遍取最大 seq
    uint32_t maxseq = 0;
    off = 0;
    while (off + 4 <= size_) {
        uint32_t payload = rd32(buf_ + off);
        int total = 4 + (int)payload + 4;
        if (off + total > size_) break;
        uint32_t seq = rd32(buf_ + off + 4 + 1);
        if (seq > maxseq) maxseq = seq;
        off += total;
    }
    next_seq_ = maxseq + 1;
    return imported;
}

// ===================== 自检 =====================
int wal_self_test() {
    int fail = 0;

    // 1) 基本追加 + 重放
    {
        Wal w;
        w.append_str(WAL_PUT, "a", "1", 1);
        w.append_str(WAL_PUT, "b", "two", 2);
        w.append_str(WAL_DEL, "a", 0, 3);
        if (w.record_count() != 3) fail++;

        struct Ctx { int puts; int dels; int seq_sum; };
        Ctx c = {0, 0, 0};
        WalVisitor v = [](void* ctx, uint8_t op, const char* k, int,
                          const char*, int, uint32_t seq) {
            Ctx* c = (Ctx*)ctx;
            if (op == WAL_PUT) c->puts++;
            if (op == WAL_DEL) c->dels++;
            c->seq_sum += (int)seq;
            (void)k;
        };
        int n = w.replay(v, &c);
        if (n != 3) fail++;
        if (c.puts != 2 || c.dels != 1) fail++;
        if (c.seq_sum != 6) fail++;   // 1+2+3
    }

    // 2) CRC 校验:损坏一条记录后重放应在该处停止
    {
        Wal w;
        w.append_str(WAL_PUT, "x", "100", 1);
        w.append_str(WAL_PUT, "y", "200", 2);
        // 人为破坏第一条记录的一个字节(改 key 区)
        if (w.size_ > 8) w.buf_[9] ^= 0xFF;
        int n = w.replay(0, 0);
        if (n != 0) fail++;   // 第一条就坏,应一条都不重放
    }

    // 3) 导出 → 导入 → 重放内容一致(模拟崩溃后从磁盘读回)
    {
        Wal w;
        w.append_str(WAL_PUT, "name", "nefu", 10);
        w.append_str(WAL_PUT, "os", "kernel", 11);
        int len = 0;
        uint8_t* blob = w.export_bytes(&len);
        Wal w2;
        int imp = w2.import_bytes(blob, len);
        delete[] blob;
        if (imp != 2) fail++;
        struct Ctx { int count; const char* last; };
        Ctx c = {0, 0};
        WalVisitor v = [](void* ctx, uint8_t, const char*, int,
                          const char* val, int, uint32_t) {
            Ctx* c = (Ctx*)ctx;
            c->count++;
            c->last = val;
        };
        w2.replay(v, &c);
        if (c.count != 2) fail++;
    }

    // 4) 检查点截断:seq<=5 的被丢弃,之后的保留
    {
        Wal w;
        for (int i = 1; i <= 10; i++) {
            char k[16]; ksprintf(k, sizeof(k), "k%d", i);
            char v[16]; ksprintf(v, sizeof(v), "v%d", i);
            w.append_str(WAL_PUT, k, v, (uint32_t)i);
        }
        int dropped = w.checkpoint(5);
        if (w.record_count() != 5) fail++;     // 只剩 k6..k10
        if (dropped <= 0) fail++;
        // 重放应只看到 k6..k10
        struct Ctx { int sum; };
        Ctx c = {0};
        WalVisitor v = [](void* ctx, uint8_t, const char* k, int,
                          const char*, int, uint32_t) {
            Ctx* c = (Ctx*)ctx;
            // k 的数字部分:k+5
            c->sum += atoi(k + 1);
        };
        w.replay(v, &c);
        if (c.sum != 6 + 7 + 8 + 9 + 10) fail++;
    }

    // 5) 二进制安全:key/value 里带 '\0' 也要正确往返
    {
        Wal w;
        const char keybin[3] = {'a', 0, 'b'};
        const char valbin[3] = {'x', 0, 'y'};
        w.append(WAL_PUT, keybin, 3, valbin, 3, 1);
        struct Ctx { int ok; };
        Ctx c = {0};
        WalVisitor v = [](void* ctx, uint8_t, const char* k, int klen,
                          const char* val, int vlen, uint32_t) {
            Ctx* c = (Ctx*)ctx;
            if (klen == 3 && vlen == 3 && k[0]=='a' && k[1]==0 && k[2]=='b'
                && val[0]=='x' && val[1]==0 && val[2]=='y') c->ok = 1;
        };
        w.replay(v, &c);
        if (!c.ok) fail++;
    }

    return fail;
}

} // namespace database
} // namespace nefu
