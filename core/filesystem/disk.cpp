// nefuOS 文件系统库 —— 虚拟磁盘(块设备)实现
#include "disk.h"
#include "../klib/klib.h"   // nefu::memcpy / new[]/delete[]

namespace nefu {
namespace filesystem {

// ===================== 小端读写辅助 =====================
// 所有磁盘结构(MBR/GPT/FAT/ext2 目录项)都按小端序列化,与 x86 一致。
static inline void wr16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v); p[1] = (uint8_t)(v >> 8);
}
static inline void wr32d(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v); p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static inline void wr64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}
static inline uint16_t rd16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static inline uint32_t rd32d(const uint8_t* p) {
    return (uint32_t)p[0]        | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16)| ((uint32_t)p[3] << 24);
}
static inline uint64_t rd64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= ((uint64_t)p[i]) << (8 * i);
    return v;
}

// 安全清零:手动字节循环 + volatile 存,规避 MinGW -O2 把 memset 展开成死循环的坑。
static inline void zbc(void* dst, int n) {
    volatile uint8_t* d = (volatile uint8_t*)dst;
    while (n-- > 0) *d++ = 0;
}

// ===================== 构造 / 析构 =====================
Disk::Disk()
    : buf_(0), nsec_(0), bad_map_(0), bad_map_bytes_(0),
      bad_count_(0), remap_count_(0),
      spare_start_(0), spare_count_(0), spare_used_(0), style_(PS_RAW) {
}

Disk::~Disk() {
    if (buf_) delete[] buf_;
    if (bad_map_) delete[] bad_map_;
    buf_ = 0; bad_map_ = 0;
}

bool Disk::create(uint32_t nsectors) {
    if (nsectors == 0) return false;
    if (buf_) { delete[] buf_; buf_ = 0; }
    if (bad_map_) { delete[] bad_map_; bad_map_ = 0; }

    nsec_ = nsectors;
    uint32_t bytes = nsectors * SECTOR_SIZE;
    buf_ = new uint8_t[bytes];
    if (!buf_) { nsec_ = 0; return false; }
    zbc(buf_, (int)bytes);

    bad_map_bytes_ = (nsectors + 7) / 8;
    bad_map_ = new uint8_t[bad_map_bytes_];
    if (!bad_map_) { bad_map_bytes_ = 0; return false; }
    zbc(bad_map_, (int)bad_map_bytes_);

    // 末尾留备用扇区做坏块重定向池
    spare_count_ = 64;
    if (spare_count_ > nsec_ / 4) spare_count_ = nsec_ / 4;
    if (spare_count_ < 4) spare_count_ = 4;
    spare_start_ = nsec_ - spare_count_;
    spare_used_ = 0;
    bad_count_ = 0;
    remap_count_ = 0;
    style_ = PS_RAW;
    return true;
}

// ===================== 坏块位图 =====================
void Disk::bad_bit_set(uint32_t lba) {
    if (lba >= nsec_) return;
    bad_map_[lba >> 3] |= (uint8_t)(1u << (lba & 7));
}
bool Disk::bad_bit_get(uint32_t lba) const {
    if (lba >= nsec_) return false;
    return (bad_map_[lba >> 3] >> (lba & 7)) & 1u;
}

void Disk::mark_bad(uint32_t lba) {
    if (lba >= nsec_) return;
    if (!bad_bit_get(lba)) {
        bad_bit_set(lba);
        bad_count_++;
    }
}
bool Disk::is_bad(uint32_t lba) const {
    if (lba >= nsec_) return false;
    return bad_bit_get(lba);
}

// 逻辑 LBA -> 物理扇区。坏块映射到备用池固定槽(真实盘用 FM 表,这里演示原理)。
uint32_t Disk::remap(uint32_t lba) const {
    if (!bad_bit_get(lba)) return lba;
    return spare_start_ + (lba % spare_count_);
}

// ===================== 块设备读写 =====================
bool Disk::read_sector(uint32_t lba, uint8_t* out) {
    if (lba >= nsec_ || !out) return false;
    uint32_t phys = remap(lba);
    if (phys != lba) remap_count_++;
    const uint8_t* src = buf_ + (uint64_t)phys * SECTOR_SIZE;
    nefu::memcpy(out, src, SECTOR_SIZE);
    return true;
}

bool Disk::write_sector(uint32_t lba, const uint8_t* data) {
    if (lba >= nsec_ || !data) return false;
    uint32_t phys = remap(lba);
    if (phys != lba) remap_count_++;
    uint8_t* dst = buf_ + (uint64_t)phys * SECTOR_SIZE;
    nefu::memcpy(dst, data, SECTOR_SIZE);
    return true;
}

bool Disk::read_bytes(uint64_t offset, uint8_t* out, uint32_t len) {
    if (!buf_ || !out) return false;
    if (offset + len > total_bytes()) return false;
    uint32_t sec0 = (uint32_t)(offset / SECTOR_SIZE);
    uint32_t off0 = (uint32_t)(offset % SECTOR_SIZE);
    uint32_t done = 0;
    uint8_t tmp[SECTOR_SIZE];
    while (done < len) {
        if (!read_sector(sec0, tmp)) return false;
        uint32_t chunk = SECTOR_SIZE - off0;
        uint32_t remain = len - done;
        if (chunk > remain) chunk = remain;
        nefu::memcpy(out + done, tmp + off0, chunk);
        done += chunk;
        sec0++;
        off0 = 0;
    }
    return true;
}

bool Disk::write_bytes(uint64_t offset, const uint8_t* data, uint32_t len) {
    if (!buf_ || !data) return false;
    if (offset + len > total_bytes()) return false;
    uint32_t sec0 = (uint32_t)(offset / SECTOR_SIZE);
    uint32_t off0 = (uint32_t)(offset % SECTOR_SIZE);
    uint32_t done = 0;
    uint8_t tmp[SECTOR_SIZE];
    while (done < len) {
        if (!read_sector(sec0, tmp)) return false;
        uint32_t chunk = SECTOR_SIZE - off0;
        uint32_t remain = len - done;
        if (chunk > remain) chunk = remain;
        nefu::memcpy(tmp + off0, data + done, chunk);
        if (!write_sector(sec0, tmp)) return false;
        done += chunk;
        sec0++;
        off0 = 0;
    }
    return true;
}

// ===================== MBR =====================
bool Disk::mbr_set_partition(int idx, uint8_t type, uint32_t lba_first,
                             uint32_t lba_count, bool bootable) {
    if (idx < 0 || idx >= 4) return false;
    if (nsec_ < 1) return false;
    uint8_t sec[SECTOR_SIZE];
    if (!read_sector(0, sec)) return false;
    MbrPartition* p = (MbrPartition*)(sec + 446 + idx * 16);
    p->status = bootable ? 0x80 : 0x00;
    p->type = type;
    p->lba_first = lba_first;
    p->lba_count = lba_count;
    p->chs_first[0] = 0xFE; p->chs_first[1] = 0xFF; p->chs_first[2] = 0xFF;
    p->chs_last[0]  = 0xFE; p->chs_last[1]  = 0xFF; p->chs_last[2]  = 0xFF;
    wr16(sec + 510, MBR_SIGNATURE);
    bool ok = write_sector(0, sec);
    if (ok) style_ = PS_MBR;
    return ok;
}

bool Disk::mbr_get_partition(int idx, MbrPartition* out) {
    if (idx < 0 || idx >= 4 || !out) return false;
    uint8_t sec[SECTOR_SIZE];
    if (!read_sector(0, sec)) return false;
    const MbrPartition* p = (const MbrPartition*)(sec + 446 + idx * 16);
    nefu::memcpy(out, p, sizeof(MbrPartition));
    return true;
}

void Disk::mbr_write_protective(uint32_t total_lba) {
    uint8_t sec[SECTOR_SIZE];
    zbc(sec, sizeof(sec));
    MbrPartition* p = (MbrPartition*)(sec + 446);
    p->status = 0x00;
    p->type = 0xEE;
    p->lba_first = 1;
    p->lba_count = total_lba > 1 ? total_lba - 1 : 1;
    p->chs_first[0] = 0xFE; p->chs_first[1] = 0xFF; p->chs_first[2] = 0xFF;
    p->chs_last[0]  = 0xFE; p->chs_last[1]  = 0xFF; p->chs_last[2]  = 0xFF;
    wr16(sec + 510, MBR_SIGNATURE);
    write_sector(0, sec);
}

// ===================== GPT =====================
bool Disk::gpt_write_protective_header(uint32_t entry_count) {
    if (nsec_ < 34) return false;
    mbr_write_protective(nsec_);

    uint8_t hdr[SECTOR_SIZE];
    zbc(hdr, sizeof(hdr));
    wr64(hdr + 0, GPT_SIGNATURE);
    wr32d(hdr + 8, 0x00010000);
    wr32d(hdr + 12, 92);
    wr32d(hdr + 16, 0);
    wr64(hdr + 24, 1);
    wr64(hdr + 32, nsec_ - 1);
    wr64(hdr + 40, 34);
    wr64(hdr + 48, nsec_ - 34);
    wr64(hdr + 72, 2);
    wr32d(hdr + 80, entry_count);
    wr32d(hdr + 84, 128);
    wr32d(hdr + 88, 0);
    write_sector(1, hdr);

    uint32_t entries_per_sec = SECTOR_SIZE / 128;
    uint32_t secs_needed = (entry_count + entries_per_sec - 1) / entries_per_sec;
    uint8_t zsec[SECTOR_SIZE];
    zbc(zsec, sizeof(zsec));
    for (uint32_t i = 0; i < secs_needed; i++) write_sector(2 + i, zsec);

    style_ = PS_GPT;
    return true;
}

bool Disk::gpt_read_header(GptHeader* out) {
    if (!out) return false;
    uint8_t hdr[SECTOR_SIZE];
    if (!read_sector(1, hdr)) return false;
    if (rd64(hdr + 0) != GPT_SIGNATURE) return false;
    out->signature = rd64(hdr + 0);
    out->revision = rd32d(hdr + 8);
    out->header_size = rd32d(hdr + 12);
    out->header_crc32 = rd32d(hdr + 16);
    out->reserved = rd32d(hdr + 20);
    out->current_lba = rd64(hdr + 24);
    out->backup_lba = rd64(hdr + 32);
    out->first_usable_lba = rd64(hdr + 40);
    out->last_usable_lba = rd64(hdr + 48);
    nefu::memcpy(out->disk_guid, hdr + 56, 16);
    out->entry_lba = rd64(hdr + 72);
    out->entry_count = rd32d(hdr + 80);
    out->entry_size = rd32d(hdr + 84);
    out->entry_array_crc32 = rd32d(hdr + 88);
    return true;
}

int Disk::gpt_add_partition(uint64_t first_lba, uint64_t last_lba, const char* name_utf16) {
    GptHeader h;
    if (!gpt_read_header(&h)) return -1;
    uint32_t entries_per_sec = SECTOR_SIZE / h.entry_size;
    uint8_t sec[SECTOR_SIZE];
    for (uint32_t i = 0; i < h.entry_count; i++) {
        uint32_t sec_no = (uint32_t)(h.entry_lba + i / entries_per_sec);
        uint32_t off = (i % entries_per_sec) * h.entry_size;
        read_sector(sec_no, sec);
        bool empty = true;
        for (int b = 0; b < 16; b++) if (sec[off + b]) { empty = false; break; }
        if (!empty) continue;
        static const uint8_t linux_guid[16] = {
            0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
            0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
        };
        nefu::memcpy(sec + off, linux_guid, 16);
        for (int b = 0; b < 16; b++) sec[off + 16 + b] = (uint8_t)(i * 31 + b);
        wr64(sec + off + 32, first_lba);
        wr64(sec + off + 40, last_lba);
        wr64(sec + off + 48, 0);
        if (name_utf16) {
            for (int b = 0; b < 36 && name_utf16[b]; b++) {
                sec[off + 56 + b * 2] = (uint8_t)name_utf16[b];
                sec[off + 57 + b * 2] = 0;
            }
        }
        write_sector(sec_no, sec);
        return (int)i;
    }
    return -1;
}

bool Disk::gpt_get_entry(int idx, GptEntry* out) {
    if (!out || idx < 0) return false;
    GptHeader h;
    if (!gpt_read_header(&h)) return false;
    if ((uint32_t)idx >= h.entry_count) return false;
    uint32_t entries_per_sec = SECTOR_SIZE / h.entry_size;
    uint32_t sec_no = (uint32_t)(h.entry_lba + idx / entries_per_sec);
    uint32_t off = (idx % entries_per_sec) * h.entry_size;
    uint8_t sec[SECTOR_SIZE];
    if (!read_sector(sec_no, sec)) return false;
    nefu::memcpy(out->type_guid, sec + off, 16);
    nefu::memcpy(out->unique_guid, sec + off + 16, 16);
    out->first_lba = rd64(sec + off + 32);
    out->last_lba = rd64(sec + off + 40);
    out->flags = rd64(sec + off + 48);
    nefu::memcpy(out->name, sec + off + 56, 72);
    return true;
}

// ===================== 镜像导出/导入 =====================
uint8_t* Disk::export_image(uint32_t* out_size) {
    if (!buf_ || nsec_ == 0) { if (out_size) *out_size = 0; return 0; }
    uint32_t sz = nsec_ * SECTOR_SIZE;
    uint8_t* img = new uint8_t[sz];
    if (!img) { if (out_size) *out_size = 0; return 0; }
    nefu::memcpy(img, buf_, sz);
    if (out_size) *out_size = sz;
    return img;
}

bool Disk::import_image(const uint8_t* data, uint32_t size) {
    if (!data || size == 0 || (size % SECTOR_SIZE) != 0) return false;
    uint32_t nsec = size / SECTOR_SIZE;
    if (!create(nsec)) return false;
    nefu::memcpy(buf_, data, size);
    return true;
}

// ===================== 磁盘布局摘要 =====================
bool disk_inspect(Disk* d, DiskReport* out) {
    if (!d || !out) return false;
    zbc(out, sizeof(DiskReport));
    out->style = d->style();
    out->total_sectors = d->total_sectors();
    out->total_bytes = d->total_bytes();
    out->bad_blocks = d->bad_count();
    out->remapped = d->remap_count();

    if (out->style == PS_MBR) {
        for (int i = 0; i < 4; i++) {
            MbrPartition mp;
            if (!d->mbr_get_partition(i, &mp)) continue;
            out->part_type[i] = mp.type;
            out->part_start[i] = mp.lba_first;
            out->part_size[i] = mp.lba_count;
            out->part_boot[i] = (mp.status == 0x80);
            if (mp.type != 0 && mp.lba_count != 0) out->mbr_partitions++;
        }
    } else if (out->style == PS_GPT) {
        GptHeader gh;
        if (d->gpt_read_header(&gh)) out->gpt_entries = gh.entry_count;
    }
    return true;
}

// ===================== 自检 =====================
int disk_self_test() {
    int fails = 0;
    Disk d;

    // 1) 创建 64 扇区空白盘,验证清零与大小
    if (!d.create(64)) return 1;
    if (d.total_sectors() != 64) fails++;
    if (d.total_bytes() != 64 * SECTOR_SIZE) fails++;
    uint8_t rdback[SECTOR_SIZE];
    d.read_sector(10, rdback);
    for (int i = 0; i < 512; i++) if (rdback[i] != 0) { fails++; break; }

    // 2) 写一个扇区再读回
    uint8_t wr[SECTOR_SIZE];
    for (int i = 0; i < 512; i++) wr[i] = (uint8_t)(i * 7 + 1);
    if (!d.write_sector(5, wr)) fails++;
    d.read_sector(5, rdback);
    for (int i = 0; i < 512; i++) if (rdback[i] != wr[i]) { fails++; break; }

    // 3) MBR 分区表(在干净的扇区 0 上做)
    if (!d.mbr_set_partition(0, 0x0E, 63, 1000, true)) fails++;
    MbrPartition mp;
    if (!d.mbr_get_partition(0, &mp)) fails++;
    if (mp.type != 0x0E) fails++;
    if (mp.lba_first != 63) fails++;
    if (mp.lba_count != 1000) fails++;
    if (mp.status != 0x80) fails++;
    if (d.style() != PS_MBR) fails++;
    if (!d.mbr_get_partition(1, &mp)) fails++;
    if (mp.type != 0) fails++;

    // 4) GPT:写保护头 + 加两个分区
    if (!d.gpt_write_protective_header(128)) { fails++; return fails; }
    GptHeader gh;
    if (!d.gpt_read_header(&gh)) fails++;
    if (gh.signature != GPT_SIGNATURE) fails++;
    if (gh.entry_count != 128) fails++;
    int e0 = d.gpt_add_partition(34, 1000, "EFI");
    int e1 = d.gpt_add_partition(1001, 2000, "ROOT");
    if (e0 != 0 || e1 != 1) fails++;
    GptEntry ge;
    if (!d.gpt_get_entry(1, &ge)) fails++;
    if (ge.first_lba != 1001) fails++;
    if (ge.last_lba != 2000) fails++;

    // 5) 跨扇区字节读写(放在扇区 40,避开 MBR/GPT 占用的 0..33)
    uint8_t pattern[1024];
    for (int i = 0; i < 1024; i++) pattern[i] = (uint8_t)(i & 0xFF);
    if (!d.write_bytes(40ull * SECTOR_SIZE, pattern, 1024)) fails++;
    uint8_t check[1024];
    if (!d.read_bytes(40ull * SECTOR_SIZE, check, 1024)) fails++;
    for (int i = 0; i < 1024; i++) if (check[i] != pattern[i]) { fails++; break; }

    // 6) 坏块管理:标记扇区 20 为坏,写读仍能拿到一致数据(经重定向)
    d.mark_bad(20);
    if (!d.is_bad(20)) fails++;
    if (d.bad_count() != 1) fails++;
    uint8_t badwr[SECTOR_SIZE];
    for (int i = 0; i < 512; i++) badwr[i] = (uint8_t)(0xA5 ^ i);
    if (!d.write_sector(20, badwr)) fails++;
    d.read_sector(20, rdback);
    for (int i = 0; i < 512; i++) if (rdback[i] != badwr[i]) { fails++; break; }

    // 7) 镜像导出/导入:写高扇区哨兵(扇区 45)
    uint8_t sentinel[SECTOR_SIZE];
    for (int i = 0; i < 512; i++) sentinel[i] = (uint8_t)(0x5A + i * 3);
    d.write_sector(45, sentinel);
    uint32_t img_sz = 0;
    uint8_t* img = d.export_image(&img_sz);
    if (!img || img_sz != 64 * SECTOR_SIZE) { fails++; }
    else {
        Disk d2;
        if (!d2.import_image(img, img_sz)) fails++;
        d2.read_sector(45, rdback);
        for (int i = 0; i < 512; i++) if (rdback[i] != sentinel[i]) { fails++; break; }
        delete[] img;
    }

    // 8) 磁盘布局摘要
    DiskReport rep;
    if (!disk_inspect(&d, &rep)) fails++;
    if (rep.total_sectors != 64) fails++;
    if (rep.style != PS_GPT) fails++;

    // 在另一块盘上验证 MBR 摘要
    Disk dm; dm.create(2000);
    dm.mbr_set_partition(0, 0x0E, 63, 1000, true);
    dm.mbr_set_partition(1, 0x83, 1100, 800, false);
    DiskReport rm;
    disk_inspect(&dm, &rm);
    if (rm.mbr_partitions != 2) fails++;
    if (rm.part_type[0] != 0x0E) fails++;
    if (rm.part_start[1] != 1100) fails++;

    // 扇区读写往返:随机写 10 个扇区再读回
    Disk d3; d3.create(2048);
    for (int i = 0; i < 10; i++) {
        uint8_t s[512];
        for (int j = 0; j < 512; j++) s[j] = (uint8_t)(i * 17 + j);
        d3.write_sector(100 + i, s);
    }
    for (int i = 0; i < 10; i++) {
        uint8_t s[512];
        d3.read_sector(100 + i, s);
        for (int j = 0; j < 512; j++) {
            if (s[j] != (uint8_t)(i * 17 + j)) { fails++; break; }
        }
    }

    // 坏块重映射:标记一个坏块后写入应被重定向
    Disk d4; d4.create(4096);
    d4.mark_bad(500);
    if (d4.bad_count() != 1) fails++;
    uint8_t b1[512]; for (int j=0;j<512;j++) b1[j]='Z';
    d4.write_sector(500, b1);
    if (d4.remap_count() < 1) fails++;

    return fails;
}

} // namespace filesystem
} // namespace nefu
