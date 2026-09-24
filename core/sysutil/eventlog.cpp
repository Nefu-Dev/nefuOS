// nefuOS 系统工具扩展库 —— 事件日志模块实现
#include "eventlog.h"
#include "../klib/klib.h"
#include <stdarg.h>

namespace nefu {
namespace sysutil {

EventLog g_syslog;

namespace {

// 事件日志自带的极小 va_list 格式化器：只支持本模块用到的 %s/%d/%u/%x/%c/%%。
// （klib 的 ksprintf 不是 va_list 版，无法直接转发。）
void ev_vsn(char* buf, int cap, const char* fmt, va_list ap) {
    if (!buf || cap <= 0) return;
    char* p = buf;
    char* end = buf + cap - 1;
    for (; *fmt && p < end; fmt++) {
        if (*fmt != '%') { *p++ = *fmt; continue; }
        fmt++;
        switch (*fmt) {
        case 's': {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "";
            while (*s && p < end) *p++ = *s++;
            break;
        }
        case 'd': {
            int v = va_arg(ap, int);
            char tmp[16]; int n = 0;
            if (v < 0) { *p++ = '-'; v = -v; if (v < 0) v = 0; }
            if (v == 0) tmp[n++] = '0';
            while (v > 0 && n < 15) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
            while (n > 0 && p < end) *p++ = tmp[--n];
            break;
        }
        case 'u': {
            unsigned int v = va_arg(ap, unsigned int);
            char tmp[16]; int n = 0;
            if (v == 0) tmp[n++] = '0';
            while (v > 0 && n < 15) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
            while (n > 0 && p < end) *p++ = tmp[--n];
            break;
        }
        case 'x': {
            unsigned int v = va_arg(ap, unsigned int);
            char tmp[16]; int n = 0;
            if (v == 0) tmp[n++] = '0';
            while (v > 0 && n < 15) { int d = v % 16; tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= 16; }
            while (n > 0 && p < end) *p++ = tmp[--n];
            break;
        }
        case 'c': *p++ = (char)va_arg(ap, int); break;
        case '%': *p++ = '%'; break;
        default: *p++ = '%'; *p++ = *fmt; break;
        }
    }
    *p = 0;
}

void copy_str(char* dst, int dstsz, const char* src) {
    if (!dst || dstsz <= 0) return;
    if (!src) src = "";
    int i = 0;
    for (; i < dstsz - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

} // namespace

EventLog::EventLog() : head_(0), count_(0), next_seq_(1), now_ms_(0), overflow_(false) {
    // 手动清零环形槽
    for (int i = 0; i < LOG_CAPACITY; i++) {
        ring_[i].ts_ms = 0;
        ring_[i].level = LOG_TRACE;
        ring_[i].seq = 0;
        ring_[i].src[0] = 0;
        ring_[i].msg[0] = 0;
    }
    for (int i = 0; i < 6; i++) total_by_level_[i] = 0;
    min_write_level_ = LOG_TRACE;
    dropped_ = 0;
}

void EventLog::push(const LogEntry& e) {
    int idx = (head_ + count_) % LOG_CAPACITY;
    if (count_ >= LOG_CAPACITY) {
        // 缓冲满：覆盖最旧一条
        head_ = (head_ + 1) % LOG_CAPACITY;
        count_--;
        overflow_ = true;
        dropped_++;
        idx = (head_ + count_) % LOG_CAPACITY;
    }
    ring_[idx] = e;
    count_++;
    if (e.level >= 0 && e.level < 6) total_by_level_[e.level]++;
}

void EventLog::write_raw(uint32_t ts, LogLevel lv, const char* src, const char* msg) {
    if (lv < min_write_level_) return;   // 低于下限不记录
    LogEntry e;
    e.ts_ms = ts;
    e.level = lv;
    e.seq = next_seq_++;
    copy_str(e.src, LOG_SRC_BYTES, src);
    copy_str(e.msg, LOG_MSG_BYTES, msg);
    push(e);
}

void EventLog::write(LogLevel lv, const char* src, const char* fmt, ...) {
    char buf[LOG_MSG_BYTES];
    va_list ap;
    va_start(ap, fmt);
    ev_vsn(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    write_raw(now_ms_, lv, src, buf);
}

int EventLog::query(const LogFilter* f, LogEntry* out, int max) const {
    int n = 0;
    for (int i = 0; i < count_ && n < max; i++) {
        int idx = (head_ + i) % LOG_CAPACITY;
        const LogEntry& e = ring_[idx];
        if (f) {
            if (e.level < f->min_level) continue;
            if (f->src_prefix[0]) {
                // 前缀匹配
                const char* s = e.src;
                const char* pre = f->src_prefix;
                bool ok = true;
                while (*pre) { if (*s != *pre || !*s) { ok = false; break; } s++; pre++; }
                if (!ok) continue;
            }
        }
        if (out) out[n] = e;
        n++;
    }
    return n;
}

int EventLog::query_range(const LogFilter* f, uint32_t from_ms, uint32_t to_ms,
                          LogEntry* out, int max) const {
    int n = 0;
    for (int i = 0; i < count_ && n < max; i++) {
        int idx = (head_ + i) % LOG_CAPACITY;
        const LogEntry& e = ring_[idx];
        if (e.ts_ms < from_ms || e.ts_ms >= to_ms) continue;
        if (f) {
            if (e.level < f->min_level) continue;
            if (f->src_prefix[0]) {
                const char* s = e.src; const char* pre = f->src_prefix;
                bool ok = true;
                while (*pre) { if (*s != *pre || !*s) { ok = false; break; } s++; pre++; }
                if (!ok) continue;
            }
        }
        if (out) out[n] = e;
        n++;
    }
    return n;
}

int EventLog::tail(int n, LogEntry* out) const {
    if (n > count_) n = count_;
    for (int i = 0; i < n; i++) {
        // 最新一条在 (head+count-1) 处
        int idx = (head_ + count_ - 1 - i) % LOG_CAPACITY;
        if (out) out[i] = ring_[idx];
    }
    return n;
}

int EventLog::total_by_level(LogLevel lv) const {
    if (lv < 0 || lv >= 6) return 0;
    return total_by_level_[lv];
}
int EventLog::count_in_range(LogLevel lv, uint32_t from_ms, uint32_t to_ms) const {
    int c = 0;
    for (int i = 0; i < count_; i++) {
        int idx = (head_ + i) % LOG_CAPACITY;
        const LogEntry& e = ring_[idx];
        if (e.level != lv) continue;
        if (e.ts_ms >= from_ms && e.ts_ms < to_ms) c++;
    }
    return c;
}
void EventLog::stats(int per_level[6]) const {
    for (int i = 0; i < 6; i++) per_level[i] = 0;
    for (int i = 0; i < count_; i++) {
        int idx = (head_ + i) % LOG_CAPACITY;
        int lv = (int)ring_[idx].level;
        if (lv >= 0 && lv < 6) per_level[lv]++;
    }
}

void EventLog::clear() {
    head_ = 0;
    count_ = 0;
    overflow_ = false;
    // 不重置 next_seq_，保证序号单调
}

int EventLog::serialize(uint8_t* out_buf, int out_cap) const {
    // 紧凑布局：magic(4) + count(4) + next_seq(4) + 每条 4+4+4+16+96 = 124 字节
    int need = 12 + count_ * (4 + 4 + 4 + LOG_SRC_BYTES + LOG_MSG_BYTES);
    if (!out_buf || out_cap < need) return 0;
    uint8_t* p = out_buf;
    // magic
    p[0] = 'N'; p[1] = 'F'; p[2] = 'L'; p[3] = 'G';
    // 手动写小端 u32
    auto put32 = [](uint8_t* q, uint32_t v) {
        q[0] = (uint8_t)(v & 0xFF);
        q[1] = (uint8_t)((v >> 8) & 0xFF);
        q[2] = (uint8_t)((v >> 16) & 0xFF);
        q[3] = (uint8_t)((v >> 24) & 0xFF);
    };
    put32(p + 4, (uint32_t)count_);
    put32(p + 8, (uint32_t)next_seq_);
    p += 12;
    for (int i = 0; i < count_; i++) {
        int idx = (head_ + i) % LOG_CAPACITY;
        const LogEntry& e = ring_[idx];
        put32(p, e.ts_ms); p += 4;
        put32(p, (uint32_t)e.level); p += 4;
        put32(p, (uint32_t)e.seq); p += 4;
        for (int k = 0; k < LOG_SRC_BYTES; k++) *p++ = (uint8_t)e.src[k];
        for (int k = 0; k < LOG_MSG_BYTES; k++) *p++ = (uint8_t)e.msg[k];
    }
    return need;
}

int EventLog::deserialize(const uint8_t* data, int len) {
    if (!data || len < 12) return 0;
    if (data[0] != 'N' || data[1] != 'F' || data[2] != 'L' || data[3] != 'G') return 0;
    auto get32 = [](const uint8_t* q) -> uint32_t {
        return (uint32_t)q[0] | ((uint32_t)q[1] << 8) |
               ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24);
    };
    uint32_t cnt = get32(data + 4);
    uint32_t nseq = get32(data + 8);
    if (cnt > LOG_CAPACITY) return 0;
    int entry_bytes = 4 + 4 + 4 + LOG_SRC_BYTES + LOG_MSG_BYTES;
    if (len < 12 + cnt * entry_bytes) return 0;
    clear();
    next_seq_ = (int)nseq;
    const uint8_t* p = data + 12;
    for (uint32_t i = 0; i < cnt; i++) {
        LogEntry e;
        e.ts_ms = get32(p); p += 4;
        e.level = (LogLevel)get32(p); p += 4;
        e.seq = (int)get32(p); p += 4;
        for (int k = 0; k < LOG_SRC_BYTES; k++) e.src[k] = (char)p[k];
        e.src[LOG_SRC_BYTES - 1] = 0;
        p += LOG_SRC_BYTES;
        for (int k = 0; k < LOG_MSG_BYTES; k++) e.msg[k] = (char)p[k];
        e.msg[LOG_MSG_BYTES - 1] = 0;
        p += LOG_MSG_BYTES;
        push(e);
    }
    return 12 + cnt * entry_bytes;
}

int filter_save(const LogFilter* f, char* buf, int cap) {
    if (!f || !buf || cap < 18) return 0;
    buf[0] = (char)f->min_level;
    int j = 1;
    for (int i = 0; i < LOG_SRC_BYTES - 1 && f->src_prefix[i] && j < cap - 1; i++)
        buf[j++] = f->src_prefix[i];
    buf[j++] = 0;
    return j;
}

int filter_load(const char* buf, int len, LogFilter* out) {
    if (!buf || !out || len < 2) return 0;
    out->min_level = (LogLevel)(unsigned char)buf[0];
    int j = 1, i = 0;
    for (; j < len && i < LOG_SRC_BYTES - 1; j++, i++) {
        if (buf[j] == 0) { out->src_prefix[i] = 0; j++; break; }
        out->src_prefix[i] = buf[j];
    }
    out->src_prefix[i] = 0;
    return j;
}
void log_format_line(const LogEntry* e, char* out, int outsz) {
    if (!e || !out || outsz < 32) { if (out && outsz > 0) out[0] = 0; return; }
    int pos = 0;
    out[pos++] = '[';
    uint32_t t = e->ts_ms;
    // 手工把毫秒数格式化进 out（ksprintf 不支持 %u 宽度补零）
    char num[12]; int nn = 0;
    if (t == 0) num[nn++] = '0';
    while (t > 0 && nn < 11) { num[nn++] = '0' + (t % 10); t /= 10; }
    for (int i = nn - 1; i >= 0 && pos < outsz - 1; i--) out[pos++] = num[i];
    out[pos++] = ']';
    out[pos++] = ' ';
    const char* lv = log_level_name(e->level);
    for (int i = 0; lv[i] && pos < outsz - 1; i++) out[pos++] = lv[i];
    out[pos++] = ' ';
    for (int i = 0; e->src[i] && pos < outsz - 1; i++) out[pos++] = e->src[i];
    out[pos++] = ':';
    out[pos++] = ' ';
    for (int i = 0; e->msg[i] && pos < outsz - 1; i++) out[pos++] = e->msg[i];
    out[pos] = 0;
}
void log_export_text(const EventLog* log, nefu::String& out) {
    if (!log) return;
    int n = log->count();
    LogFilter f; f.min_level = LOG_TRACE; f.src_prefix[0] = 0;
    LogEntry* entries = new LogEntry[n > 0 ? n : 1];
    int got = log->query(&f, entries, n);
    for (int i = 0; i < got; i++) {
        char line[160];
        log_format_line(&entries[i], line, sizeof(line));
        out += line;
        out += "\n";
    }
    delete[] entries;
}
LogFilter log_filter_min(LogLevel min_level) {
    LogFilter f;
    f.min_level = min_level;
    f.src_prefix[0] = 0;
    return f;
}
const char* log_level_name(LogLevel lv) {
    switch (lv) {
    case LOG_TRACE: return "TRACE";
    case LOG_DEBUG: return "DEBUG";
    case LOG_INFO:  return "INFO";
    case LOG_WARN:  return "WARN";
    case LOG_ERROR: return "ERROR";
    case LOG_FATAL: return "FATAL";
    }
    return "?";
}

LogLevel log_level_from_name(const char* s) {
    if (!s) return LOG_TRACE;
    if (s[0] == 'T' || s[0] == 't') return LOG_TRACE;
    if (s[0] == 'D' || s[0] == 'd') return LOG_DEBUG;
    if (s[0] == 'W' || s[0] == 'w') return LOG_WARN;
    if (s[0] == 'E' || s[0] == 'e') return LOG_ERROR;
    if (s[0] == 'F' || s[0] == 'f') return LOG_FATAL;
    return LOG_INFO;
}
// ---------------- 便捷自由函数 ----------------
void log_write(LogLevel lv, const char* src, const char* fmt, ...) {
    char buf[LOG_MSG_BYTES];
    va_list ap;
    va_start(ap, fmt);
    ev_vsn(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_syslog.write_raw(g_syslog.now(), lv, src, buf);
}

int log_query(const LogFilter* f, LogEntry* out, int max) {
    return g_syslog.query(f, out, max);
}

// ---------------- 自检 ----------------
int EventLog::self_test() {
    int fails = 0;
    EventLog log;
    log.set_now(1000);

    // 写入若干条
    log.write(LOG_INFO, "proc", "boot pid=%d name=%s", 1, "init");
    log.write(LOG_WARN, "mem", "low memory %d MB left", 128);
    log.write(LOG_ERROR, "disk", "I/O error on sda");
    log.write(LOG_DEBUG, "proc", "sched tick=%d", 42);

    if (log.count() != 4) fails++;

    // 过滤：只看 ERROR 及以上
    LogFilter f;
    f.min_level = LOG_ERROR;
    copy_str(f.src_prefix, LOG_SRC_BYTES, "");
    LogEntry buf[8];
    int n = log.query(&f, buf, 8);
    if (n != 1) fails++;
    if (n == 1 && buf[0].level != LOG_ERROR) fails++;

    // 过滤：来源前缀 "proc"
    f.min_level = LOG_TRACE;
    copy_str(f.src_prefix, LOG_SRC_BYTES, "proc");
    n = log.query(&f, buf, 8);
    if (n != 2) fails++;   // INFO boot + DEBUG sched

    // 级别统计
    int per[6];
    log.stats(per);
    if (per[LOG_INFO] != 1) fails++;
    if (per[LOG_WARN] != 1) fails++;
    if (per[LOG_ERROR] != 1) fails++;
    if (per[LOG_DEBUG] != 1) fails++;

    // 轮转：写爆容量，验证丢最旧
    log.clear();
    for (int i = 0; i < LOG_CAPACITY + 50; i++) {
        log.write(LOG_INFO, "t", "msg #%d", i);
    }
    if (log.count() != LOG_CAPACITY) fails++;
    if (!log.overflowed()) fails++;
    // 最旧一条应该是 #50（0..49 被挤掉）
    LogEntry all[LOG_CAPACITY];
    LogFilter allf; allf.min_level = LOG_TRACE; allf.src_prefix[0] = 0;
    int got = log.query(&allf, all, LOG_CAPACITY);
    if (got != LOG_CAPACITY) fails++;
    // 前面已写 4 条(seq1..4)后 clear()，再写 306 条；最旧存活是 i=50，seq=5+50=55
    if (all[0].seq != 55) fails++;

    // 持久化往返
    uint8_t blob[33000];
    int nbytes = log.serialize(blob, sizeof(blob));
    if (nbytes <= 0) fails++;
    EventLog log2;
    int rd = log2.deserialize(blob, nbytes);
    if (rd != nbytes) fails++;
    if (log2.count() != log.count()) fails++;
    // 再查一次内容一致
    LogEntry all2[LOG_CAPACITY];
    int got2 = log2.query(&allf, all2, LOG_CAPACITY);
    if (got2 != got) fails++;
    if (got2 > 0 && all2[0].seq != all[0].seq) fails++;

    // 时间范围 / tail
    EventLog l2;
    l2.set_now(0);
    l2.write_raw(1000, LOG_INFO, "a", "one");
    l2.write_raw(2000, LOG_WARN, "b", "two");
    l2.write_raw(3000, LOG_ERROR, "c", "three");
    LogFilter any; any.min_level = LOG_TRACE; any.src_prefix[0] = 0;
    LogEntry r[8];
    int rn = l2.query_range(&any, 1500, 3000, r, 8);
    if (rn != 1) fails++;                       // 只有 2000 这条落在 [1500,3000)
    int ce = l2.count_in_range(LOG_ERROR, 0, 10000);
    if (ce != 1) fails++;
    LogEntry t[3];
    int tn = l2.tail(2, t);
    if (tn != 2) fails++;
    if (t[0].ts_ms != 3000) fails++;            // 最新是 3000
    if (t[1].ts_ms != 2000) fails++;

    // 级别名往返
    if (strcmp(log_level_name(LOG_ERROR), "ERROR") != 0) fails++;
    if (log_level_from_name("warn") != LOG_WARN) fails++;
    if (log_level_from_name("fatal") != LOG_FATAL) fails++;

    // 过滤器序列化往返
    LogFilter f1;
    f1.min_level = LOG_WARN;
    int k = 0;
    for (; "net"[k]; k++) f1.src_prefix[k] = "net"[k];
    f1.src_prefix[k] = 0;
    char fbuf[24];
    int sl = filter_save(&f1, fbuf, sizeof(fbuf));
    if (sl < 5) fails++;
    LogFilter f2;
    f2.min_level = LOG_TRACE;
    f2.src_prefix[0] = 0;
    int rl = filter_load(fbuf, sl, &f2);
    if (rl == 0) fails++;
    if (f2.min_level != LOG_WARN) fails++;
    if (strcmp(f2.src_prefix, "net") != 0) fails++;

    // log_format_line
    LogEntry le;
    le.ts_ms = 1234; le.level = LOG_INFO; le.seq = 1;
    int a = 0; for (; "boot"[a]; a++) le.src[a] = "boot"[a]; le.src[a] = 0;
    a = 0; for (; "started"[a]; a++) le.msg[a] = "started"[a]; le.msg[a] = 0;
    char linebuf[160];
    log_format_line(&le, linebuf, sizeof(linebuf));
    if (linebuf[0] != '[') fails++;
    if (strstr(linebuf, "boot") == 0) fails++;

    // total_by_level
    EventLog lt;
    lt.write(LOG_ERROR, "x", "e1");
    lt.write(LOG_ERROR, "x", "e2");
    lt.write(LOG_INFO, "x", "i1");
    if (lt.total_by_level(LOG_ERROR) != 2) fails++;
    if (lt.total_by_level(LOG_INFO) != 1) fails++;
    if (lt.total_by_level(LOG_DEBUG) != 0) fails++;

    // set_min_level 过滤
    lt.set_min_level(LOG_WARN);
    lt.write(LOG_INFO, "x", "should be dropped");
    if (lt.total_by_level(LOG_INFO) != 1) fails++;   // 不新增
    if (lt.min_level() != LOG_WARN) fails++;
    lt.set_min_level(LOG_TRACE);   // 还原

    // dropped 计数：写满环形缓冲
    EventLog lr;
    for (int i = 0; i < LOG_CAPACITY + 5; i++)
        lr.write(LOG_INFO, "rot", "msg");
    if (lr.dropped() != 5) fails++;
    if (!lr.overflowed()) fails++;

    // log_export_text
    EventLog lx; lx.set_now(0);
    lx.write(LOG_INFO, "exp", "one");
    lx.write(LOG_WARN, "exp", "two");
    nefu::String txt;
    log_export_text(&lx, txt);
    if (txt.len() < 10) fails++;
    if (lx.total_writes() != 2) fails++;
    if (lx.total_by_level(LOG_INFO) != 1) fails++;

    if (nefu::strcmp(log_level_name(LOG_WARN), "WARN") != 0) fails++;
    if (log_level_from_name("error") != LOG_ERROR) fails++;
    if (log_level_from_name("nope") != LOG_INFO) fails++;
    return fails;
}

} // namespace sysutil
} // namespace nefu
