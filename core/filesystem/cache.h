// nefuOS 文件系统库 —— 缓冲区缓存(Buffer Cache)
//
// 在块设备之上加一层 LRU 缓存,减少磁盘 I/O:
//   - 命中:直接返回内存中的块
//   - 未命中:从磁盘读入,必要时按 LRU 淘汰最久未用的脏块(先回写)
//   - 写:标记脏页,延迟回写(write-back)
//   - 预读:顺序读时预取后续若干块
//   - 统计:命中数 / 未命中数 / 脏块回写数
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "disk.h"

namespace nefu {
namespace filesystem {

const int CACHE_BLOCK_SIZE = 512;
const int CACHE_DEFAULT_BLOCKS = 16;

// 缓存统计
struct CacheStats {
    uint32_t reads;
    uint32_t hits;
    uint32_t misses;
    uint32_t writes;
    uint32_t dirty_flushed;
    uint32_t readahead;
    uint32_t evictions;
};

class BlockCache {
public:
    BlockCache();
    ~BlockCache();

    // 绑定到磁盘,缓存块数 nblocks。
    bool attach(Disk* disk, int nblocks = CACHE_DEFAULT_BLOCKS);

    // 读一个扇区到 buf。命中缓存则不读盘。
    bool read(uint32_t lba, uint8_t* buf);
    // 写一个扇区(标记脏,延迟回写)
    bool write(uint32_t lba, const uint8_t* buf);
    // 强制回写所有脏块
    void flush();
    // 回写并清空缓存
    void invalidate();

    // 预读:顺序访问时预取 ahead 个后续块
    void readahead(uint32_t lba, int ahead);

    const CacheStats& stats() const { return stats_; }
    float hit_ratio() const {
        if (stats_.reads == 0) return 0.0f;
        return (float)stats_.hits / (float)stats_.reads;
    }
    int dirty_count() const;

    // 写穿策略开关:开启后每次 write 立即落盘(不延迟)。默认 write-back。
    void set_write_through(bool on) { write_through_ = on; }
    bool write_through() const { return write_through_; }

private:
    struct Slot {
        uint32_t lba;
        bool     valid;
        bool     dirty;
        uint32_t seq;       // 访问序号,用于 LRU
        uint8_t  data[CACHE_BLOCK_SIZE];
    };
    Slot*   slots_;
    int     nblocks_;
    uint32_t seq_;
    Disk*   disk_;
    CacheStats stats_;
    bool    write_through_;

    int  find_slot(uint32_t lba);       // 命中返回 idx,-1 否则
    int  evict_lru();                   // 选一个受害者,回写脏块,返回 idx
};

int cache_self_test();

} // namespace filesystem
} // namespace nefu
