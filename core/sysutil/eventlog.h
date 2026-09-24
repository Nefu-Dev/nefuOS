// nefuOS 系统工具扩展库 —— 事件日志模块
// 分级日志写入 / 查询 / 过滤 / 环形轮转 / 二进制持久化。
// 日志存放于固定容量的环形缓冲，写满后丢弃最旧记录（轮转），避免无界增长。
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace sysutil {

// 日志级别
enum LogLevel {
    LOG_TRACE = 0,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
};

const int LOG_SRC_BYTES = 16;
const int LOG_MSG_BYTES = 96;
const int LOG_CAPACITY  = 256;     // 环形缓冲容量

struct LogEntry {
    uint32_t ts_ms;                // 记录时刻（虚拟时钟）
    LogLevel level;
    int      seq;                  // 全局序号
    char     src[LOG_SRC_BYTES];   // 来源子系统，如 "proc"/"svc"
    char     msg[LOG_MSG_BYTES];   // 消息正文
};

// 日志过滤器：按级别下限 + 来源子串过滤。
struct LogFilter {
    LogLevel min_level;            // 只记 >= min_level 的
    char     src_prefix[LOG_SRC_BYTES]; // 来源前缀过滤，空串 = 不过滤
};

class EventLog {
public:
    EventLog();

    // 写入一条日志（printf 风格内部用 ksprintf 展开）。
    void write(LogLevel lv, const char* src, const char* fmt, ...);
    // 直接写入一条已经格式化好的记录。
    void write_raw(uint32_t ts, LogLevel lv, const char* src, const char* msg);

    // 查询：按过滤器导出匹配的记录到 out（最多 max 条），返回条数。
    int  query(const LogFilter* f, LogEntry* out, int max) const;
    // 时间范围查询：只导出 [from_ms, to_ms) 内的记录（配合过滤器）。
    int  query_range(const LogFilter* f, uint32_t from_ms, uint32_t to_ms,
                     LogEntry* out, int max) const;
    // tail：返回最新的 n 条（按时间倒序填充到 out）。
    int  tail(int n, LogEntry* out) const;
    // 统计某级别在某时间范围内的条数。
    int  count_in_range(LogLevel lv, uint32_t from_ms, uint32_t to_ms) const;
    // 按级别统计各档条数。
    void stats(int per_level[6]) const;
    // 某一级别累计条数（含已轮转丢弃的）。
    int  total_by_level(LogLevel lv) const;
    int  count() const { return count_; }
    void clear();

    // 轮转策略：容量写满时自动丢最旧的（在 write 里实现）。
    // 这里暴露当前是否曾经溢出过。
    bool overflowed() const { return overflow_; }
    // 因环形缓冲满而被丢弃的记录总数。
    uint32_t dropped() const { return dropped_; }
    // 累计写入总数（含被丢弃的）。
    uint32_t total_writes() const { return next_seq_ - 1; }

    // 持久化：把整个环形缓冲序列化成紧凑二进制流到 out_buf（调用方分配，
    // 至少 out_cap 字节）。返回写出的字节数；不够则返回 0。
    int  serialize(uint8_t* out_buf, int out_cap) const;
    // 从 serialize 产生的缓冲恢复。返回读取字节数，0 = 数据损坏。
    int  deserialize(const uint8_t* data, int len);

    // 虚拟时钟挂钩：日志时间戳取自这里。
    void set_now(uint32_t ms) { now_ms_ = ms; }
    uint32_t now() const { return now_ms_; }
    // 写入级别下限：低于此级别不记录。
    void set_min_level(LogLevel lv) { min_write_level_ = lv; }
    LogLevel min_level() const { return min_write_level_; }

    // 自检
    int self_test();

private:
    void push(const LogEntry& e);
    LogEntry ring_[LOG_CAPACITY];
    int head_;          // 最旧一条的下标
    int count_;         // 当前条数（<= LOG_CAPACITY）
    int next_seq_;
    uint32_t now_ms_;
    bool overflow_;
    int  total_by_level_[6];   // 各级别累计写入计数
    LogLevel min_write_level_;   // 写入级别下限
    uint32_t dropped_;             // 丢弃计数
};

// 过滤器序列化：把 f 打包到 buf（至少 20 字节），返回写出长度。
int  filter_save(const LogFilter* f, char* buf, int cap);
// 把一条记录渲染成单行文本 "[ts] LEVEL src: msg" 到 out（outsz >= 128）。
void log_format_line(const LogEntry* e, char* out, int outsz);
// 把全部记录按时间顺序导出为多行文本（追加到 out）。
void log_export_text(const EventLog* log, nefu::String& out);
// 快捷过滤器：只要 >= min_level 的记录。
LogFilter log_filter_min(LogLevel min_level);
// 从 filter_save 产生的 buf 恢复过滤器。返回读取长度，0 = 损坏。
int  filter_load(const char* buf, int len, LogFilter* out);

// 级别名转换
const char* log_level_name(LogLevel lv);
LogLevel     log_level_from_name(const char* s);

// 全局默认日志
extern EventLog g_syslog;

// 便捷自由函数
void log_write(LogLevel lv, const char* src, const char* fmt, ...);
int  log_query(const LogFilter* f, LogEntry* out, int max);

} // namespace sysutil
} // namespace nefu
