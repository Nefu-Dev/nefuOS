// nefuOS 文件系统库 —— 缓冲区缓存实现
#include "cache.h"
#include "../klib/klib.h"

namespace nefu {
namespace filesystem {

static inline void zbc(void* dst, int n) {
    volatile uint8_t* d=(volatile uint8_t*)dst; while(n-->0) *d++=0;
}

BlockCache::BlockCache() : slots_(0), nblocks_(0), seq_(0), disk_(0), write_through_(false) {
    zbc(&stats_, sizeof(stats_));
}
BlockCache::~BlockCache() {
    if (slots_) { delete[] slots_; slots_ = 0; }
}

bool BlockCache::attach(Disk* disk, int nblocks) {
    disk_ = disk;
    nblocks_ = nblocks;
    slots_ = new Slot[nblocks_];
    for (int i = 0; i < nblocks_; i++) {
        slots_[i].valid = false;
        slots_[i].dirty = false;
        slots_[i].lba = 0;
        slots_[i].seq = 0;
    }
    zbc(&stats_, sizeof(stats_));
    return true;
}

int BlockCache::find_slot(uint32_t lba) {
    for (int i = 0; i < nblocks_; i++) {
        if (slots_[i].valid && slots_[i].lba == lba) return i;
    }
    return -1;
}

int BlockCache::evict_lru() {
    // 找 seq 最小(最久未用)的有效槽
    int victim = -1;
    uint32_t minseq = 0xFFFFFFFF;
    for (int i = 0; i < nblocks_; i++) {
        if (!slots_[i].valid) return i; // 空槽直接用
        if (slots_[i].seq < minseq) { minseq = slots_[i].seq; victim = i; }
    }
    if (victim >= 0 && slots_[victim].dirty) {
        // 回写脏块
        disk_->write_sector(slots_[victim].lba, slots_[victim].data);
        stats_.dirty_flushed++;
        slots_[victim].dirty = false;
    }
    stats_.evictions++;
    return victim;
}

bool BlockCache::read(uint32_t lba, uint8_t* buf) {
    stats_.reads++;
    int idx = find_slot(lba);
    if (idx >= 0) {
        stats_.hits++;
        slots_[idx].seq = ++seq_;
        nefu::memcpy(buf, slots_[idx].data, CACHE_BLOCK_SIZE);
        return true;
    }
    stats_.misses++;
    // 读盘
    if (!disk_->read_sector(lba, buf)) return false;
    // 找槽位放入
    int slot = evict_lru();
    if (slot < 0) return false;
    slots_[slot].valid = true;
    slots_[slot].dirty = false;
    slots_[slot].lba = lba;
    slots_[slot].seq = ++seq_;
    nefu::memcpy(slots_[slot].data, buf, CACHE_BLOCK_SIZE);
    return true;
}

bool BlockCache::write(uint32_t lba, const uint8_t* buf) {
    stats_.writes++;
    int idx = find_slot(lba);
    if (idx < 0) {
        idx = evict_lru();
        if (idx < 0) return false;
        slots_[idx].valid = true;
        slots_[idx].lba = lba;
    }
    slots_[idx].dirty = true;
    slots_[idx].seq = ++seq_;
    nefu::memcpy(slots_[idx].data, buf, CACHE_BLOCK_SIZE);
    // 写穿策略:立即落盘,不延迟
    if (write_through_) {
        disk_->write_sector(lba, buf);
        slots_[idx].dirty = false;
    }
    return true;
}

void BlockCache::flush() {
    for (int i = 0; i < nblocks_; i++) {
        if (slots_[i].valid && slots_[i].dirty) {
            disk_->write_sector(slots_[i].lba, slots_[i].data);
            slots_[i].dirty = false;
            stats_.dirty_flushed++;
        }
    }
}
void BlockCache::invalidate() {
    flush();
    for (int i = 0; i < nblocks_; i++) slots_[i].valid = false;
}

void BlockCache::readahead(uint32_t lba, int ahead) {
    uint8_t tmp[CACHE_BLOCK_SIZE];
    for (int i = 1; i <= ahead; i++) {
        uint32_t blk = lba + i;
        if (find_slot(blk) >= 0) continue; // 已在缓存
        if (disk_->read_sector(blk, tmp)) {
            stats_.readahead++;
            int slot = evict_lru();
            if (slot < 0) break;
            slots_[slot].valid = true;
            slots_[slot].dirty = false;
            slots_[slot].lba = blk;
            slots_[slot].seq = ++seq_;
            nefu::memcpy(slots_[slot].data, tmp, CACHE_BLOCK_SIZE);
        }
    }
}

int BlockCache::dirty_count() const {
    int n = 0;
    for (int i = 0; i < nblocks_; i++)
        if (slots_[i].valid && slots_[i].dirty) n++;
    return n;
}

int cache_self_test() {
    int fails = 0;
    Disk disk; disk.create(1024);
    // 先写一些扇区
    uint8_t buf[512];
    for (int i = 0; i < 10; i++) {
        zbc(buf, sizeof(buf));
        buf[0] = (uint8_t)('0' + i);
        disk.write_sector(i, buf);
    }

    BlockCache cache;
    if (!cache.attach(&disk, 4)) return 1;

    // 第一次读扇区 0:miss
    uint8_t rb[512];
    if (!cache.read(0, rb)) fails++;
    if (rb[0] != '0') fails++;
    if (cache.stats().misses != 1) fails++;

    // 再读扇区 0:hit
    cache.read(0, rb);
    if (cache.stats().hits != 1) fails++;

    // 写扇区 5(脏)
    zbc(buf, sizeof(buf)); buf[0] = 'X';
    cache.write(5, buf);
    if (cache.dirty_count() != 1) fails++;
    // 扇区 5 在磁盘上还没更新(write-back)
    disk.read_sector(5, rb);
    if (rb[0] == 'X') fails++; // 应为旧值

    // flush 后,磁盘应更新
    cache.flush();
    if (cache.dirty_count() != 0) fails++;
    disk.read_sector(5, rb);
    if (rb[0] != 'X') fails++;

    // LRU 淘汰:缓存只有 4 槽,读 0..7 应触发淘汰
    for (int i = 0; i < 8; i++) {
        zbc(buf, sizeof(buf));
        buf[0] = (uint8_t)('a' + i);
        cache.write(100+i, buf);
    }
    cache.flush();
    // 读回验证
    for (int i = 0; i < 8; i++) {
        disk.read_sector(100+i, rb);
        if (rb[0] != (uint8_t)('a'+i)) { fails++; break; }
    }

    // 命中率应 > 0
    if (cache.hit_ratio() <= 0.0f) fails++;

    // 写穿策略:开启后 write 立即落盘
    BlockCache wt; wt.attach(&disk, 4);
    wt.set_write_through(true);
    zbc(buf, sizeof(buf)); buf[0] = 'W';
    wt.write(200, buf);
    disk.read_sector(200, rb);
    if (rb[0] != 'W') fails++;
    if (wt.dirty_count() != 0) fails++;

    // 预读:顺序读时预取后续块
    BlockCache rc; rc.attach(&disk, 8);
    for (int i = 0; i < 4; i++) {
        zbc(buf, sizeof(buf)); buf[0] = (uint8_t)('R'+i);
        disk.write_sector(300+i, buf);
    }
    rc.readahead(300, 3);
    if (rc.stats().readahead < 3) fails++;
    // 预取后读 301 应命中
    rc.read(301, rb);
    if (rb[0] != 'R'+1) fails++;

    // invalidate 后所有块失效
    rc.invalidate();
    if (rc.dirty_count() != 0) fails++;

    return fails;
}

} // namespace filesystem
} // namespace nefu
