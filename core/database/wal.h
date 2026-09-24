// nefuOS 嵌入式数据库 —— Write-Ahead Log (预写日志)
//
// 设计目标:
//   在把修改真正应用到内存哈希表 / B+ 树之前,先把这条修改 "追加" 到日志里。
//   崩溃后,只要日志还在,就能通过 "重放(replay)" 把所有已提交的修改重做一遍,
//   从而恢复到崩溃前的状态。这就是经典 WAL(Write-Ahead Logging)。
//
// 本模块为 "内存模式" 实现:日志内容放在一块可增长的字节缓冲里,不依赖任何 VFS / 磁盘。
// 这样既能在裸机上跑,也能在宿主机单元测试里独立编译验证。要接真实磁盘时,
// 只需把 append() 里写缓冲的那一步换成写块设备即可。
//
// 日志记录的二进制格式(小端,全部字段紧跟其后):
//
//   +-----------+-----------+-----------+-----------+-----------+----------------+----------------+-----------+
//   | payload_len(4)| op(1) | seq(4)    | klen(4)   | vlen(4)   | key[klen]      | value[vlen]    | crc(4)    |
//   +-----------+-----------+-----------+-----------+-----------+----------------+----------------+-----------+
//
//   payload_len : 从 op 到 value 末尾的字节数(不含 payload_len 自身与 crc),用于一步跳过坏记录
//   op          : 操作类型,见 WalOp
//   seq         : 单调递增的日志序号(LSN),用于检查点截断与事务排序
//   klen / vlen : key / value 长度(字节),支持二进制安全(中间允许 '\0')
//   crc         : 对 op..value 全部字节计算的 CRC32,校验损坏记录
//
// 检查点(checkpoint):
//   做完一次完整快照后,调用 checkpoint(lsn),把日志里序号 <= lsn 的记录整体截断,
//   因为快照已经包含了它们的效果,重放时只需从快照之后继续。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace database {

// ---- 日志操作类型 ----
enum WalOp : uint8_t {
    WAL_NONE     = 0,   // 占位
    WAL_PUT      = 1,   // 写入 / 覆盖一个键值
    WAL_DEL      = 2,   // 删除一个键
    WAL_BEGIN    = 3,   // 事务开始标记
    WAL_COMMIT   = 4,   // 事务提交标记
    WAL_CHECKPT  = 5,   // 检查点标记(逻辑上等于一次截断点)
};

// ---- 重放时每条记录交给回调函数 ----
//   ctx    : 调用者自定义指针
//   op     : 操作类型
//   key    : key 数据指针(指向日志内部缓冲,仅在回调内有效)
//   klen   : key 长度
//   value  : value 数据指针(DEL 时可能为空)
//   vlen   : value 长度
//   seq    : 日志序号
typedef void (*WalVisitor)(void* ctx, uint8_t op,
                          const char* key, int klen,
                          const char* value, int vlen,
                          uint32_t seq);

// ---- 预写日志 ----
class Wal {
public:
    Wal();
    ~Wal();

    // 追加一条记录。key/value 二进制安全。成功返回写入的字节数(含头),失败返回 -1。
    int append(uint8_t op, const char* key, int klen,
               const char* value, int vlen, uint32_t seq);

    // 便捷重载(C 字符串,自动算长度)
    int append_str(uint8_t op, const char* key, const char* value, uint32_t seq);

    // 顺序重放整个日志。逐条校验 CRC,损坏即停止并返回已成功重放的记录数。
    int replay(WalVisitor vis, void* ctx);

    // 检查点:丢弃所有 seq <= stable_lsn 的记录(它们已被快照吸收)。
    // 返回实际截断的字节数。
    int checkpoint(uint32_t stable_lsn);

    // 清空整个日志(等于从头开始)
    void truncate_all();

    // 当前日志字节数 / 记录数 / 下一个应使用的 seq
    int      size() const { return size_; }
    int      record_count() const { return count_; }
    uint32_t next_seq() const { return next_seq_; }
    void     set_next_seq(uint32_t s) { next_seq_ = s; }

    // 把整个日志序列化成一块独立的新缓冲(供持久化 / 网络传输)。
    // 调用者负责用 delete[] 释放返回的缓冲。out_len 输出长度。
    uint8_t* export_bytes(int* out_len) const;

    // 从一块外部字节数据重建日志(崩溃后从磁盘读回时用)。
    // 会先清空现有日志,再逐条校验 CRC 后拷入。返回导入的记录数。
    int import_bytes(const uint8_t* data, int len);

    // 自检需要直接戳缓冲做损坏注入
    friend int wal_self_test();

private:
    // 确保缓冲至少还能再容纳 need 字节
    void grow(int need);

    uint8_t* buf_;      // 日志字节缓冲
    int      size_;     // 已用字节
    int      cap_;      // 容量
    int      count_;    // 记录条数
    uint32_t next_seq_; // 下一条记录的序号
};

// CRC32(多项式 0xEDB88320),供 WAL 与需要校验的模块共用
uint32_t wal_crc32(const void* data, int len);

// 模块自检:返回失败断言数(0 表示全部通过)
int wal_self_test();

} // namespace database
} // namespace nefu
