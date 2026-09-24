// nefuOS 文件系统库 —— WAL 日志实现
#include "journal.h"
#include "../klib/klib.h"

namespace nefu {
namespace filesystem {

static inline void w16(uint8_t* p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static inline void w32(uint8_t* p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static inline uint16_t r16(const uint8_t* p) { return (uint16_t)(p[0]|(p[1]<<8)); }
static inline uint32_t r32(const uint8_t* p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static inline void zbc(void* dst, int n) {
    volatile uint8_t* d=(volatile uint8_t*)dst; while(n-->0) *d++=0;
}

Journal::Journal() : disk_(0), log_start_(0), log_sectors_(0),
    log_head_(0), log_tail_(0), next_txn_(1) {
    zbc(&stats_, sizeof(stats_));
}
Journal::~Journal() {}

bool Journal::open(Disk* disk, uint32_t log_start, uint32_t log_sectors) {
    disk_ = disk; log_start_ = log_start; log_sectors_ = log_sectors;
    log_head_ = 0; log_tail_ = 0; next_txn_ = 1;
    zbc(&stats_, sizeof(stats_));
    return true;
}
void Journal::close() { checkpoint(); }

bool Journal::append_header(uint16_t type, uint32_t txn, uint32_t lba) {
    if (log_tail_ >= log_sectors_) return false;
    uint8_t sec[512]; zbc(sec, sizeof(sec));
    w16(sec+0, JOURNAL_MAGIC);
    w16(sec+2, type);
    w32(sec+4, txn);
    w32(sec+8, lba);
    if (!disk_->write_sector(log_start_ + log_tail_, sec)) return false;
    log_tail_++;
    return true;
}
bool Journal::append_data(const uint8_t* data512) {
    if (log_tail_ >= log_sectors_) return false;
    if (!disk_->write_sector(log_start_ + log_tail_, data512)) return false;
    log_tail_++;
    return true;
}

uint32_t Journal::begin_txn() {
    uint32_t txn = next_txn_++;
    if (!append_header(J_BEGIN, txn, 0)) return 0;
    stats_.txns_started++;
    return txn;
}
bool Journal::log_write(uint32_t txn, uint32_t lba, const uint8_t* data512) {
    if (!append_header(J_BLOCK_WRITE, txn, lba)) return false;
    if (!append_data(data512)) return false;
    return true;
}
bool Journal::commit(uint32_t txn) {
    if (!append_header(J_COMMIT, txn, 0)) return false;
    stats_.txns_committed++;
    return true;
}
bool Journal::checkpoint() {
    if (!append_header(J_CHECKPOINT, 0, 0)) return false;
    // 截断:head 移到 tail(简化,不真正擦除)
    log_head_ = log_tail_;
    return true;
}

uint32_t Journal::recover() {
    // 扫描日志,收集每条 BLOCK_WRITE 及其所属事务,记录哪些事务已 COMMIT。
    // 恢复时只回放已提交事务的写;未提交事务的写丢弃(undo)。
    struct PendingWrite { uint32_t txn; uint32_t lba; uint8_t data[512]; };
    PendingWrite writes[64];
    uint32_t write_count = 0;
    bool committed_txn[32]; uint32_t committed_list[32];
    uint32_t committed_count = 0;
    for (int i = 0; i < 32; i++) { committed_txn[i] = false; committed_list[i] = 0; }

    uint32_t idx = 0;
    uint32_t active_txn = 0;
    while (idx < log_tail_) {
        uint8_t sec[512];
        disk_->read_sector(log_start_ + idx, sec);
        if (r16(sec+0) != JOURNAL_MAGIC) break;
        uint16_t type = r16(sec+2);
        uint32_t txn = r32(sec+4);
        uint32_t lba = r32(sec+8);
        idx++;
        if (type == J_BEGIN) {
            active_txn = txn;
        } else if (type == J_BLOCK_WRITE) {
            uint8_t dsec[512];
            disk_->read_sector(log_start_ + idx, dsec);
            if (write_count < 64) {
                writes[write_count].txn = active_txn;
                writes[write_count].lba = lba;
                nefu::memcpy(writes[write_count].data, dsec, 512);
                write_count++;
            }
            idx++;
        } else if (type == J_COMMIT) {
            if (committed_count < 32) {
                committed_list[committed_count] = txn;
                committed_txn[committed_count] = true;
                committed_count++;
            }
        } else if (type == J_CHECKPOINT) {
            // 检查点之前的写都已落盘,清空 pending
            write_count = 0;
        }
    }
    // 回放:只回放已提交事务的写
    uint32_t redone_txns = 0;
    for (uint32_t i = 0; i < write_count; i++) {
        bool is_committed = false;
        for (uint32_t c = 0; c < committed_count; c++)
            if (committed_list[c] == writes[i].txn) { is_committed = true; break; }
        if (is_committed) {
            disk_->write_sector(writes[i].lba, writes[i].data);
            stats_.blocks_redone++;
        }
    }
    stats_.txns_recovered = committed_count;
    stats_.txns_aborted = 0; // 简化
    return committed_count;
}

int journal_self_test() {
    int fails = 0;
    Disk disk; disk.create(2048);
    // 日志区放在扇区 1000..1511(512 扇区),数据区用 0..999
    Journal j;
    if (!j.open(&disk, 1000, 512)) return 1;

    // 初始:扇区 0 = "AAA"
    uint8_t s0[512]; zbc(s0, sizeof(s0)); s0[0]='A'; s0[1]='A'; s0[2]='A';
    disk.write_sector(0, s0);

    // 事务 1:把扇区 0 改成 "BBB"
    uint32_t t1 = j.begin_txn();
    if (t1 == 0) fails++;
    uint8_t s0b[512]; zbc(s0b, sizeof(s0b)); s0b[0]='B'; s0b[1]='B'; s0b[2]='B';
    if (!j.log_write(t1, 0, s0b)) fails++;
    if (!j.commit(t1)) fails++;

    // 事务 2:写扇区 5 = "CCC"(未提交,模拟崩溃)
    uint32_t t2 = j.begin_txn();
    uint8_t s5[512]; zbc(s5, sizeof(s5)); s5[0]='C'; s5[1]='C'; s5[2]='C';
    j.log_write(t2, 5, s5);
    // 不 commit —— 模拟崩溃

    // 现在直接读扇区 0,应该还是 "AAA"(因为事务 1 的写还没真正落盘)
    uint8_t check[512];
    disk.read_sector(0, check);
    if (check[0] != 'A') fails++;

    // 恢复:回放已提交事务(事务1)
    uint32_t rec = j.recover();
    if (rec != 1) fails++; // 应回放 1 个事务
    // 扇区 0 应变成 "BBB"
    disk.read_sector(0, check);
    if (check[0] != 'B') fails++;
    // 扇区 5 应仍是全 0(事务2未提交,不回放)
    disk.read_sector(5, check);
    if (check[0] != 0) fails++;

    // checkpoint 后再开事务
    j.checkpoint();
    uint32_t t3 = j.begin_txn();
    uint8_t s7[512]; zbc(s7, sizeof(s7)); s7[0]='D';
    j.log_write(t3, 7, s7);
    j.commit(t3);
    j.recover();
    disk.read_sector(7, check);
    if (check[0] != 'D') fails++;

    // 多事务压力:连续提交 5 个事务,每个写一个扇区,然后整体 recover
    Journal j2;
    if (!j2.open(&disk, 1000, 1024)) { fails++; return fails; }
    for (int tx = 0; tx < 5; tx++) {
        uint32_t t2 = j2.begin_txn();
        uint8_t dd[512];
        for (int i = 0; i < 512; i++) dd[i] = (uint8_t)(0x40 + tx);
        j2.log_write(t2, (uint32_t)(20 + tx), dd);
        j2.commit(t2);
    }
    // 再开一个未提交事务
    uint32_t tub = j2.begin_txn();
    uint8_t ub[512]; for (int i=0;i<512;i++) ub[i]=0xEE;
    j2.log_write(tub, 99, ub);
    // recover
    uint32_t rc2 = j2.recover();
    if (rc2 != 5) fails++;
    for (int tx = 0; tx < 5; tx++) {
        disk.read_sector(20 + tx, check);
        if (check[0] != (uint8_t)(0x40 + tx)) { fails++; break; }
    }
    disk.read_sector(99, check);
    if (check[0] != 0) fails++; // 未提交,不应回放

    return fails;
}

} // namespace filesystem
} // namespace nefu
