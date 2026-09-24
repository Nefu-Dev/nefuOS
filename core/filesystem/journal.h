// nefuOS 文件系统库 —— 日志文件系统(Write-Ahead Logging)
//
// 实现经典 WAL(写前日志):所有对块设备的修改先记入日志,再落盘。
// 崩溃后通过回放(redo)已提交事务、丢弃(undo)未提交事务来恢复一致性。
//
// 日志记录布局(线性追加,每条记录占 1~2 个扇区):
//   扇区 0(头,16 字节有效):
//     magic   0x1976
//     type    BEGIN / BLOCK_WRITE / COMMIT / CHECKPOINT
//     txn_id  事务号
//     lba     目标逻辑块号(BLOCK_WRITE 时有效)
//   若 type == BLOCK_WRITE,扇区 1 紧跟 512 字节原始块数据。
//
// 日志在磁盘上占一段连续扇区:[log_start, log_start+log_sectors)。
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "disk.h"

namespace nefu {
namespace filesystem {

const uint16_t JOURNAL_MAGIC = 0x1976;

enum JournalRecType {
    J_BEGIN = 1,
    J_BLOCK_WRITE = 2,
    J_COMMIT = 3,
    J_CHECKPOINT = 4
};

// 日志记录头(16 字节,写在扇区前 16 字节)
struct JournalHeader {
    uint16_t magic;
    uint16_t type;
    uint32_t txn_id;
    uint32_t lba;
    uint32_t pad;
};

// 恢复统计
struct JournalStats {
    uint32_t txns_started;
    uint32_t txns_committed;
    uint32_t txns_recovered;   // 崩溃后回放的已提交事务数
    uint32_t txns_aborted;     // 未提交被丢弃的事务数
    uint32_t blocks_redone;
    uint32_t log_used_sectors;
};

class Journal {
public:
    Journal();
    ~Journal();

    bool open(Disk* disk, uint32_t log_start, uint32_t log_sectors);
    void close();

    uint32_t begin_txn();
    bool log_write(uint32_t txn, uint32_t lba, const uint8_t* data512);
    bool commit(uint32_t txn);
    bool checkpoint();

    // 崩溃恢复:回放所有已 COMMIT 的事务。返回回放的事务数。
    uint32_t recover();

    const JournalStats& stats() const { return stats_; }
    uint32_t log_length() const { return log_tail_ - log_head_; }

private:
    bool append_header(uint16_t type, uint32_t txn, uint32_t lba);
    bool append_data(const uint8_t* data512);

    Disk*    disk_;
    uint32_t log_start_;
    uint32_t log_sectors_;
    uint32_t log_head_;
    uint32_t log_tail_;
    uint32_t next_txn_;
    JournalStats stats_;
};

int journal_self_test();

} // namespace filesystem
} // namespace nefu
