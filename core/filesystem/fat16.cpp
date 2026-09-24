// nefuOS 文件系统库 —— FAT16 文件系统实现
#include "fat16.h"
#include "../klib/klib.h"

namespace nefu {
namespace filesystem {

// 小端读写(与 disk.cpp 一致,这里本地再写一份,保持模块独立)
static inline void w16(uint8_t* p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static inline void w32(uint8_t* p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static inline uint16_t r16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1]<<8));
}
static inline uint32_t r32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static inline void zbc(void* dst, int n) {
    volatile uint8_t* d = (volatile uint8_t*)dst;
    while (n-- > 0) *d++ = 0;
}

Fat16::Fat16()
    : disk_(0), part_start_(0),
      bytes_per_sector_(512), sectors_per_cluster_(1),
      reserved_sectors_(1), num_fats_(2), root_entries_(512),
      total_sectors_(0), media_descriptor_(0xF8), sectors_per_fat_(1) {
}

Fat16::~Fat16() { disk_ = 0; }

bool Fat16::rd(uint32_t lba, uint8_t* buf) {
    return disk_->read_sector(part_start_ + lba, buf);
}
bool Fat16::wr(uint32_t lba, const uint8_t* buf) {
    return disk_->write_sector(part_start_ + lba, buf);
}

// ===================== 格式化 =====================
bool Fat16::format(Disk* disk, uint32_t start_lba, uint32_t total_lba,
                   uint16_t spc) {
    if (!disk || total_lba < 64) return false;
    // 仅接受合法的簇大小
    if (spc != 1 && spc != 2 && spc != 4 && spc != 8 &&
        spc != 16 && spc != 32 && spc != 64 && spc != 128) return false;

    disk_ = disk;
    part_start_ = start_lba;
    bytes_per_sector_ = 512;
    sectors_per_cluster_ = (uint8_t)spc;
    num_fats_ = 2;
    root_entries_ = 512;
    total_sectors_ = (uint16_t)total_lba;
    media_descriptor_ = 0xF8;

    // 计算每个 FAT 占多少扇区:
    // 数据簇数 = (total - reserved - root_sectors) / spc
    // FAT16 表项数 = 簇数 + 2;字节数 = (簇数+2)*2;扇区数 = 上取整 /512
    // 迭代求解一次即可(FAT 大小反过来影响布局)。
    reserved_sectors_ = 1; // 引导扇区占 1
    uint32_t root_secs = (root_entries_ * 32 + 511) / 512;
    // 先估一个 spf
    uint32_t data_secs = total_lba - reserved_sectors_ - root_secs;
    uint32_t clusters = data_secs / spc;
    // FAT 字节数
    uint32_t fat_bytes = (clusters + 2) * 2;
    sectors_per_fat_ = (uint16_t)((fat_bytes + 511) / 512);
    // 重新算一次(因为 FAT 区本身占扇区)
    data_secs = total_lba - reserved_sectors_ - num_fats_ * sectors_per_fat_ - root_secs;
    clusters = data_secs / spc;
    fat_bytes = (clusters + 2) * 2;
    sectors_per_fat_ = (uint16_t)((fat_bytes + 511) / 512);

    // 写引导扇区 / BPB
    uint8_t bs[512];
    zbc(bs, sizeof(bs));
    // 跳转指令占位
    bs[0] = 0xEB; bs[1] = 0x3C; bs[2] = 0x90;
    // OEM 名
    bs[3] = 'N'; bs[4] = 'E'; bs[5] = 'F'; bs[6] = 'U'; bs[7] = 'O'; bs[8] = 'S'; bs[9] = ' '; bs[10] = ' ';
    w16(bs + 11, 512);            // bytes per sector
    bs[13] = (uint8_t)spc;        // sectors per cluster
    w16(bs + 14, reserved_sectors_);
    bs[16] = num_fats_;
    w16(bs + 17, root_entries_);
    w16(bs + 19, (uint16_t)total_lba);
    bs[21] = media_descriptor_;
    w16(bs + 22, sectors_per_fat_);
    w16(bs + 24, 1);             // sectors per track
    w16(bs + 26, 1);             // heads
    w16(bs + 28, 0);             // hidden sectors
    w16(bs + 30, 0);             // total sectors32 (小盘不用)
    bs[38] = 0x29;               // boot signature
    w32(bs + 39, 0x12345678);    // volume serial
    bs[43] = 'N'; bs[44] = 'E'; bs[45] = 'F'; bs[46] = 'U'; bs[47] = 'D'; bs[48] = 'I'; bs[49] = 'S'; bs[50] = 'K';
    bs[51] = ' '; bs[52] = ' '; bs[53] = ' ';
    // "FAT16   " 类型
    bs[54]='F'; bs[55]='A'; bs[56]='T'; bs[57]='1'; bs[58]='6'; bs[59]=' '; bs[60]=' '; bs[61]=' ';
    w16(bs + 510, 0xAA55);
    if (!wr(0, bs)) return false;

    // 清两个 FAT
    uint8_t zsec[512];
    zbc(zsec, sizeof(zsec));
    for (uint8_t f = 0; f < num_fats_; f++) {
        uint32_t fat_lba = reserved_sectors_ + f * sectors_per_fat_;
        for (uint16_t i = 0; i < sectors_per_fat_; i++) wr(fat_lba + i, zsec);
    }
    // FAT 头两项:介质描述符 + EOC
    uint8_t fat0[512];
    rd(reserved_sectors_, fat0);
    w16(fat0 + 0, 0xFF00 | media_descriptor_);  // 簇 0
    w16(fat0 + 2, 0xFFFF);                      // 簇 1 = EOC
    wr(reserved_sectors_, fat0);
    if (num_fats_ > 1) {
        uint32_t fat1_lba = reserved_sectors_ + sectors_per_fat_;
        wr(fat1_lba, fat0);
    }

    // 清根目录
    for (uint32_t i = 0; i < root_secs; i++) wr(root_dir_lba() + i, zsec);

    return true;
}

// ===================== 挂载 =====================
bool Fat16::mount(Disk* disk, uint32_t start_lba) {
    disk_ = disk;
    part_start_ = start_lba;
    uint8_t bs[512];
    if (!rd(0, bs)) return false;
    if (r16(bs + 510) != 0xAA55) return false;
    bytes_per_sector_ = r16(bs + 11);
    sectors_per_cluster_ = bs[13];
    reserved_sectors_ = r16(bs + 14);
    num_fats_ = bs[16];
    root_entries_ = r16(bs + 17);
    total_sectors_ = r16(bs + 19);
    media_descriptor_ = bs[21];
    sectors_per_fat_ = r16(bs + 22);
    if (bytes_per_sector_ != 512) return false;
    if (sectors_per_cluster_ == 0) return false;
    return true;
}

// ===================== FAT 表项 =====================
uint16_t Fat16::fat_get(uint16_t cluster) {
    // 每个 FAT 扇区 512 字节 = 256 个表项
    uint32_t off = (uint32_t)cluster * 2;
    uint32_t fat_lba = reserved_sectors_ + off / 512;
    uint32_t sec_off = off % 512;
    uint8_t sec[512];
    if (!rd(fat_lba, sec)) return 0;
    return r16(sec + sec_off);
}

void Fat16::fat_set(uint16_t cluster, uint16_t value) {
    uint32_t off = (uint32_t)cluster * 2;
    uint32_t fat_lba = reserved_sectors_ + off / 512;
    uint32_t sec_off = off % 512;
    uint8_t sec[512];
    if (!rd(fat_lba, sec)) return;
    w16(sec + sec_off, value);
    wr(fat_lba, sec);
    // 同步第二份 FAT
    if (num_fats_ > 1) wr(fat_lba + sectors_per_fat_, sec);
}

// ===================== 簇分配 =====================
uint32_t Fat16::cluster_to_lba(uint16_t cluster) {
    // 簇 2 对应数据区起点
    return data_lba() + ((uint32_t)cluster - 2) * sectors_per_cluster_;
}

uint16_t Fat16::alloc_cluster() {
    uint32_t total = total_clusters();
    for (uint32_t c = 2; c < total + 2; c++) {
        if (fat_get((uint16_t)c) == FAT_FREE) {
            fat_set((uint16_t)c, FAT_EOF);
            return (uint16_t)c;
        }
    }
    return 0; // 满了
}

void Fat16::free_chain(uint16_t cluster) {
    uint16_t c = cluster;
    while (c >= 2 && c < 0xFFF8) {
        uint16_t next = fat_get(c);
        fat_set(c, FAT_FREE);
        c = next;
    }
}

uint16_t Fat16::chain_append(uint16_t cluster) {
    uint16_t c = alloc_cluster();
    if (c == 0) return 0;
    fat_set(cluster, c); // 把旧尾指向新簇
    return c;
}

uint32_t Fat16::total_clusters() {
    uint32_t root_secs = root_dir_sectors();
    uint32_t data_secs = total_sectors_ - reserved_sectors_ - num_fats_ * sectors_per_fat_ - root_secs;
    return data_secs / sectors_per_cluster_;
}

uint32_t Fat16::free_clusters() {
    uint32_t total = total_clusters();
    uint32_t free = 0;
    for (uint32_t c = 2; c < total + 2; c++)
        if (fat_get((uint16_t)c) == FAT_FREE) free++;
    return free;
}

uint16_t Fat16::bytes_per_cluster() {
    return (uint16_t)(512 * sectors_per_cluster_);
}

// 简化 fsck:扫描整个 FAT 表,分类统计每个簇。
int Fat16::fsck(FsckReport* out) {
    if (!out) return 1;
    zbc(out, sizeof(*out));
    uint32_t total = total_clusters();
    out->total_clusters = total;
    for (uint32_t c = 2; c < total + 2; c++) {
        uint16_t v = fat_get((uint16_t)c);
        if (v == FAT_FREE) out->free_clusters++;
        else if (v == FAT_BAD) out->bad_clusters++;
        else out->used_clusters++;
    }
    uint32_t slots = (uint32_t)root_entries_;
    uint8_t sec[512];
    for (uint32_t s = 0; s < slots; s++) {
        uint32_t lba = root_dir_lba() + s / 16;
        if ((s % 16) == 0) rd(lba, sec);
        FatDirEntry* e = (FatDirEntry*)(sec + (s % 16) * 32);
        if (e->name[0] == 0x00) break;
        if (e->name[0] == 0xE5) continue;
        out->dir_entries++;
    }
    uint32_t sum = out->used_clusters + out->free_clusters + out->bad_clusters;
    int problems = 0;
    if (sum != total) problems++;
    return problems;
}

// ===================== 名字打包 =====================
void Fat16::pack_name(const char* name, uint8_t out[11]) {
    // name 形如 "FILE.TXT";拆成 8 + 3,空格填充,统一转大写(FAT 约定)
    for (int i = 0; i < 11; i++) out[i] = ' ';
    if (!name) return;
    const char* dot = 0;
    for (const char* p = name; *p; p++) if (*p == '.') dot = p;
    int mainlen = dot ? (int)(dot - name) : (int)0;
    int mi = 0;
    for (int i = 0; i < mainlen && i < 8; i++) {
        uint8_t c = (uint8_t)name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[mi++] = c;
    }
    if (dot) {
        int ei = 8;
        for (const char* e = dot + 1; *e && ei < 11; e++) {
            uint8_t c = (uint8_t)*e;
            if (c >= 'a' && c <= 'z') c -= 32;
            out[ei++] = c;
        }
    }
}

void Fat16::unpack_name(const uint8_t in[11], char out[13]) {
    int n = 0;
    int i = 0;
    for (i = 0; i < 8; i++) {
        if (in[i] == ' ' || in[i] == 0) break;
        out[n++] = (char)in[i];
    }
    // 扩展名
    bool hasext = false;
    for (i = 8; i < 11; i++) if (in[i] != ' ' && in[i] != 0) { hasext = true; break; }
    if (hasext) {
        out[n++] = '.';
        for (i = 8; i < 11; i++) {
            if (in[i] == ' ' || in[i] == 0) break;
            out[n++] = (char)in[i];
        }
    }
    out[n] = 0;
}

// ===================== 目录槽读写 =====================
// parent_cluster==0 表示根目录(固定扇区区)。
// 用一个缓冲读目录的"当前扇区",槽偏移按 32 字节/项。
long Fat16::find_empty_dir_slot(uint16_t parent_cluster) {
    // 根目录:固定大小,逐项扫
    if (parent_cluster == 0) {
        uint32_t slots = (uint32_t)root_entries_;
        uint8_t sec[512];
        for (uint32_t s = 0; s < slots; s++) {
            uint32_t lba = root_dir_lba() + s / 16; // 16 项/扇区
            if ((s % 16) == 0) rd(lba, sec);
            FatDirEntry* e = (FatDirEntry*)(sec + (s % 16) * 32);
            if (e->name[0] == 0x00 || e->name[0] == 0xE5) {
                // 写回这个扇区(调用方负责真正写;这里返回偏移)
                return (long)s * 32;
            }
        }
        return -1;
    }
    // 子目录:沿簇链扫
    uint16_t c = parent_cluster;
    long off = 0;
    uint8_t sec[512];
    while (c >= 2 && c < 0xFFF8) {
        rd(cluster_to_lba(c), sec);
        for (int i = 0; i < 16; i++) {
            FatDirEntry* e = (FatDirEntry*)(sec + i * 32);
            if (e->name[0] == 0x00 || e->name[0] == 0xE5) {
                return off + i * 32;
            }
        }
        off += 512;
        c = fat_get(c);
    }
    // 没找到空槽,需要扩目录:追加一个簇
    return -2; // -2 表示需要扩簇
}

bool Fat16::read_dir_slot(uint16_t parent_cluster, long slot_off, FatDirEntry* e) {
    if (parent_cluster == 0) {
        uint32_t s = (uint32_t)(slot_off / 32);
        uint32_t lba = root_dir_lba() + s / 16;
        uint8_t sec[512];
        if (!rd(lba, sec)) return false;
        nefu::memcpy(e, sec + (s % 16) * 32, 32);
        return true;
    }
    // 子目录:沿链定位
    uint16_t c = parent_cluster;
    long acc = 0;
    while (c >= 2 && c < 0xFFF8) {
        if (slot_off >= acc && slot_off < acc + 512) {
            uint8_t sec[512];
            if (!rd(cluster_to_lba(c), sec)) return false;
            nefu::memcpy(e, sec + (slot_off - acc), 32);
            return true;
        }
        acc += 512;
        c = fat_get(c);
    }
    return false;
}

bool Fat16::write_dir_slot(uint16_t parent_cluster, long slot_off, const FatDirEntry* e) {
    if (parent_cluster == 0) {
        uint32_t s = (uint32_t)(slot_off / 32);
        uint32_t lba = root_dir_lba() + s / 16;
        uint8_t sec[512];
        if (!rd(lba, sec)) return false;
        nefu::memcpy(sec + (s % 16) * 32, e, 32);
        return wr(lba, sec);
    }
    uint16_t c = parent_cluster;
    long acc = 0;
    while (c >= 2 && c < 0xFFF8) {
        if (slot_off >= acc && slot_off < acc + 512) {
            uint8_t sec[512];
            if (!rd(cluster_to_lba(c), sec)) return false;
            nefu::memcpy(sec + (slot_off - acc), e, 32);
            return wr(cluster_to_lba(c), sec);
        }
        acc += 512;
        c = fat_get(c);
    }
    return false;
}

// ===================== 路径解析 =====================
bool Fat16::resolve_parent(const char* path, uint16_t* parent_cluster,
                           char* name_out, int name_sz) {
    if (!path || path[0] != '/') return false;
    // 复制到可改缓冲
    char buf[128];
    int i = 0;
    for (; path[i] && i < 127; i++) buf[i] = path[i];
    buf[i] = 0;
    // 找最后一个 '/'
    int last_slash = -1;
    for (int j = 0; buf[j]; j++) if (buf[j] == '/') last_slash = j;
    if (last_slash < 0) return false;
    // 父路径:buf[0..last_slash)
    buf[last_slash] = 0;
    const char* parent_path = (last_slash == 0) ? "/" : buf;
    // 末级名:buf[last_slash+1..]
    const char* leaf = buf + last_slash + 1;
    if (!leaf[0]) return false;

    // 从根出发逐层走父路径
    uint16_t cur = 0; // 根目录
    if (last_slash != 0) {
        // 遍历 parent_path 的每一段
        const char* p = parent_path;
        while (*p == '/') p++;
        char seg[32];
        while (*p) {
            int k = 0;
            while (*p && *p != '/' && k < 31) seg[k++] = *p++;
            seg[k] = 0;
            while (*p == '/') p++;
            FatDirInfo info;
            if (!find_entry(cur, seg, &info)) return false;
            if (!info.is_dir) return false;
            cur = info.start_cluster;
        }
    }
    *parent_cluster = cur;
    int n = 0;
    while (leaf[n] && n < name_sz - 1) { name_out[n] = leaf[n]; n++; }
    name_out[n] = 0;
    return true;
}

// ===================== 查找 =====================
bool Fat16::find_entry(uint16_t parent_cluster, const char* name, FatDirInfo* out) {
    // 构造目标 8.3 名
    uint8_t target[11];
    pack_name(name, target);
    // 规范化比较名
    char tname[13];
    unpack_name(target, tname);

    // 根目录
    if (parent_cluster == 0) {
        uint32_t slots = (uint32_t)root_entries_;
        uint8_t sec[512];
        for (uint32_t s = 0; s < slots; s++) {
            uint32_t lba = root_dir_lba() + s / 16;
            if ((s % 16) == 0) rd(lba, sec);
            FatDirEntry* e = (FatDirEntry*)(sec + (s % 16) * 32);
            if (e->name[0] == 0x00) break;       // 到末尾了
            if (e->name[0] == 0xE5) continue;     // 已删除
            if (e->attr & FAT_ATTR_VOLLABEL) continue;
            char ename[13];
            unpack_name(e->name, ename);
            // 比较
            bool eq = true;
            for (int k = 0; tname[k] || ename[k]; k++) {
                char a = tname[k], b = ename[k];
                if (a >= 'a' && a <= 'z') a -= 32;
                if (b >= 'a' && b <= 'z') b -= 32;
                if (a != b) { eq = false; break; }
            }
            if (eq) {
                if (out) {
                    unpack_name(e->name, out->name);
                    out->is_dir = (e->attr & FAT_ATTR_DIR) != 0;
                    out->is_deleted = false;
                    out->start_cluster = (uint16_t)(e->cluster_hi | e->cluster_lo);
                    out->size = e->size;
                }
                return true;
            }
        }
        return false;
    }
    // 子目录
    uint16_t c = parent_cluster;
    uint8_t sec[512];
    while (c >= 2 && c < 0xFFF8) {
        rd(cluster_to_lba(c), sec);
        for (int i = 0; i < 16; i++) {
            FatDirEntry* e = (FatDirEntry*)(sec + i * 32);
            if (e->name[0] == 0x00) return false;
            if (e->name[0] == 0xE5) continue;
            char ename[13];
            unpack_name(e->name, ename);
            bool eq = true;
            for (int k = 0; tname[k] || ename[k]; k++) {
                char a = tname[k], b = ename[k];
                if (a >= 'a' && a <= 'z') a -= 32;
                if (b >= 'a' && b <= 'z') b -= 32;
                if (a != b) { eq = false; break; }
            }
            if (eq) {
                if (out) {
                    unpack_name(e->name, out->name);
                    out->is_dir = (e->attr & FAT_ATTR_DIR) != 0;
                    out->is_deleted = false;
                    out->start_cluster = (uint16_t)(e->cluster_hi | e->cluster_lo);
                    out->size = e->size;
                }
                return true;
            }
        }
        c = fat_get(c);
    }
    return false;
}

// ===================== 创建文件/目录 =====================
bool Fat16::create_file(const char* path) {
    uint16_t parent;
    char leaf[32];
    if (!resolve_parent(path, &parent, leaf, sizeof(leaf))) return false;
    // 已存在?
    FatDirInfo info;
    if (find_entry(parent, leaf, &info)) return false;

    long slot = find_empty_dir_slot(parent);
    if (slot == -1) return false;       // 根目录满
    if (slot == -2) {
        // 子目录需要扩簇:先给父目录追加一个簇
        uint16_t nc = chain_append(parent);
        if (nc == 0) return false;
        slot = 512; // 新簇起始处
    }

    uint16_t cl = alloc_cluster();
    if (cl == 0) return false;

    FatDirEntry e;
    zbc(&e, sizeof(e));
    pack_name(leaf, e.name);
    e.attr = FAT_ATTR_ARCHIVE;
    e.cluster_hi = 0;
    e.cluster_lo = cl;
    e.size = 0;
    return write_dir_slot(parent, slot, &e);
}

bool Fat16::mkdir(const char* path) {
    uint16_t parent;
    char leaf[32];
    if (!resolve_parent(path, &parent, leaf, sizeof(leaf))) return false;
    FatDirInfo info;
    if (find_entry(parent, leaf, &info)) return false;

    long slot = find_empty_dir_slot(parent);
    if (slot == -1) return false;
    if (slot == -2) {
        uint16_t nc = chain_append(parent);
        if (nc == 0) return false;
        slot = 512;
    }

    uint16_t cl = alloc_cluster();
    if (cl == 0) return false;

    // 新目录簇清零,写 "." 和 ".." 两个特殊项
    uint8_t zsec[512];
    zbc(zsec, sizeof(zsec));
    wr(cluster_to_lba(cl), zsec);
    FatDirEntry* dot = (FatDirEntry*)zsec;
    dot->name[0] = '.';
    for (int i = 1; i < 8; i++) dot->name[i] = ' ';
    for (int i = 8; i < 11; i++) dot->name[i] = ' ';
    dot->attr = FAT_ATTR_DIR;
    dot->cluster_lo = cl;
    dot->cluster_hi = 0;
    FatDirEntry* dotdot = (FatDirEntry*)(zsec + 32);
    dotdot->name[0] = '.'; dotdot->name[1] = '.';
    for (int i = 2; i < 8; i++) dotdot->name[i] = ' ';
    for (int i = 8; i < 11; i++) dotdot->name[i] = ' ';
    dotdot->attr = FAT_ATTR_DIR;
    dotdot->cluster_lo = parent; // 根目录父=0
    dotdot->cluster_hi = 0;
    wr(cluster_to_lba(cl), zsec);

    // 在父目录里登记这个新目录
    FatDirEntry e;
    zbc(&e, sizeof(e));
    pack_name(leaf, e.name);
    e.attr = FAT_ATTR_DIR;
    e.cluster_lo = cl;
    return write_dir_slot(parent, slot, &e);
}

// ===================== 删除 =====================
bool Fat16::remove(const char* path) {
    uint16_t parent;
    char leaf[32];
    if (!resolve_parent(path, &parent, leaf, sizeof(leaf))) return false;
    // 找到它的目录项偏移
    uint8_t target[11];
    pack_name(leaf, target);
    uint16_t c = parent;
    long acc = 0;
    bool found = false;
    long found_slot = 0;
    uint16_t found_cluster = 0;
    uint8_t found_attr = 0;
    uint8_t sec[512];
    if (parent == 0) {
        uint32_t slots = (uint32_t)root_entries_;
        for (uint32_t s = 0; s < slots; s++) {
            uint32_t lba = root_dir_lba() + s / 16;
            if ((s % 16) == 0) rd(lba, sec);
            FatDirEntry* e = (FatDirEntry*)(sec + (s % 16) * 32);
            if (e->name[0] == 0x00) break;
            if (e->name[0] == 0xE5) continue;
            if (e->attr & FAT_ATTR_VOLLABEL) continue;
            if (memcmp(e->name, target, 11) == 0) {
                found = true; found_slot = (long)s * 32;
                found_cluster = (uint16_t)(e->cluster_hi | e->cluster_lo);
                found_attr = e->attr;
                // 标记删除
                e->name[0] = 0xE5;
                wr(lba, sec);
                break;
            }
        }
    } else {
        while (c >= 2 && c < 0xFFF8) {
            rd(cluster_to_lba(c), sec);
            for (int i = 0; i < 16; i++) {
                FatDirEntry* e = (FatDirEntry*)(sec + i * 32);
                if (e->name[0] == 0x00) break;
                if (e->name[0] == 0xE5) continue;
                if (memcmp(e->name, target, 11) == 0) {
                    found = true; found_slot = acc + i * 32;
                    found_cluster = (uint16_t)(e->cluster_hi | e->cluster_lo);
                    found_attr = e->attr;
                    e->name[0] = 0xE5;
                    wr(cluster_to_lba(c), sec);
                    goto done;
                }
            }
            acc += 512;
            c = fat_get(c);
        }
    }
done:
    if (!found) return false;
    // 目录不能非空删(简化:直接删)。释放簇链。
    free_chain(found_cluster);
    return true;
}

// ===================== 重命名 / 截断 / 属性 =====================
bool Fat16::rename(const char* oldpath, const char* newname) {
    uint16_t parent;
    char leaf[32];
    if (!resolve_parent(oldpath, &parent, leaf, sizeof(leaf))) return false;
    FatDirInfo existing;
    if (find_entry(parent, newname, &existing)) return false;
    uint8_t target[11]; pack_name(leaf, target);
    uint8_t newname11[11]; pack_name(newname, newname11);
    if (parent == 0) {
        uint32_t slots = (uint32_t)root_entries_;
        uint8_t sec[512];
        for (uint32_t s = 0; s < slots; s++) {
            uint32_t lba = root_dir_lba() + s / 16;
            if ((s % 16) == 0) rd(lba, sec);
            FatDirEntry* e = (FatDirEntry*)(sec + (s % 16) * 32);
            if (e->name[0] == 0x00) break;
            if (e->name[0] == 0xE5) continue;
            if (memcmp(e->name, target, 11) == 0) {
                nefu::memcpy(e->name, newname11, 11);
                wr(lba, sec);
                return true;
            }
        }
    } else {
        uint16_t c = parent; uint8_t sec[512];
        while (c >= 2 && c < 0xFFF8) {
            rd(cluster_to_lba(c), sec);
            for (int i = 0; i < 16; i++) {
                FatDirEntry* e = (FatDirEntry*)(sec + i * 32);
                if (e->name[0] == 0x00) break;
                if (e->name[0] == 0xE5) continue;
                if (memcmp(e->name, target, 11) == 0) {
                    nefu::memcpy(e->name, newname11, 11);
                    wr(cluster_to_lba(c), sec);
                    return true;
                }
            }
            c = fat_get(c);
        }
    }
    return false;
}

bool Fat16::truncate(const char* path, uint32_t new_size) {
    FatDirInfo info;
    uint16_t parent; char leaf[32];
    if (!resolve_parent(path, &parent, leaf, sizeof(leaf))) return false;
    if (!find_entry(parent, leaf, &info)) return false;
    if (info.is_dir) return false;
    uint32_t bpc = bytes_per_cluster();
    uint16_t cur = info.start_cluster;
    uint32_t new_clusters = (new_size + bpc - 1) / bpc;
    if (new_clusters == 0) new_clusters = 1;
    for (uint32_t i = 1; i < new_clusters && cur >= 2 && cur < 0xFFF8; i++)
        cur = fat_get(cur);
    if (cur >= 2 && cur < 0xFFF8) {
        uint16_t nxt = fat_get(cur);
        fat_set(cur, FAT_EOF);
        if (nxt >= 2) free_chain(nxt);
    }
    uint8_t target[11]; pack_name(leaf, target);
    if (parent == 0) {
        uint32_t slots = (uint32_t)root_entries_; uint8_t sec[512];
        for (uint32_t s = 0; s < slots; s++) {
            uint32_t lba = root_dir_lba() + s / 16;
            if ((s % 16) == 0) rd(lba, sec);
            FatDirEntry* e = (FatDirEntry*)(sec + (s % 16) * 32);
            if (e->name[0] == 0x00) break;
            if (memcmp(e->name, target, 11) == 0) { e->size = new_size; wr(lba, sec); break; }
        }
    }
    return true;
}

bool Fat16::stat(const char* path, FatDirInfo* out) {
    uint16_t parent; char leaf[32];
    if (!resolve_parent(path, &parent, leaf, sizeof(leaf))) return false;
    return find_entry(parent, leaf, out);
}

// ===================== 读/写文件 =====================
long Fat16::file_size(const char* path) {
    FatDirInfo info;
    uint16_t parent;
    char leaf[32];
    if (!resolve_parent(path, &parent, leaf, sizeof(leaf))) return -1;
    if (!find_entry(parent, leaf, &info)) return -1;
    return (long)info.size;
}

int Fat16::read_file(const char* path, uint8_t* buf, uint32_t bufsz) {
    FatDirInfo info;
    uint16_t parent;
    char leaf[32];
    if (!resolve_parent(path, &parent, leaf, sizeof(leaf))) return -1;
    if (!find_entry(parent, leaf, &info)) return -1;
    if (info.is_dir) return -1;
    uint32_t to_read = info.size;
    if (to_read > bufsz) to_read = bufsz;
    uint16_t c = info.start_cluster;
    uint32_t done = 0;
    uint32_t bpc = bytes_per_cluster();
    uint8_t csec[512];
    while (c >= 2 && c < 0xFFF8 && done < to_read) {
        uint32_t cbytes = bpc;
        if (done + cbytes > to_read) cbytes = to_read - done;
        // 逐扇区拷
        uint32_t secdone = 0;
        while (secdone < cbytes) {
            rd(cluster_to_lba(c) + secdone / 512, csec);
            uint32_t off = secdone % 512;
            uint32_t chunk = 512 - off;
            if (secdone + chunk > cbytes) chunk = cbytes - secdone;
            nefu::memcpy(buf + done + secdone, csec + off, chunk);
            secdone += chunk;
        }
        done += cbytes;
        c = fat_get(c);
    }
    return (int)done;
}

bool Fat16::write_file(const char* path, const uint8_t* data, uint32_t len) {
    FatDirInfo info;
    uint16_t parent;
    char leaf[32];
    if (!resolve_parent(path, &parent, leaf, sizeof(leaf))) return false;
    bool existed = find_entry(parent, leaf, &info);
    uint16_t start_cluster = 0;
    if (existed) {
        if (info.is_dir) return false;
        // 释放旧链
        free_chain(info.start_cluster);
        start_cluster = info.start_cluster;
    } else {
        // 创建新项
        long slot = find_empty_dir_slot(parent);
        if (slot == -1) return false;
        if (slot == -2) {
            uint16_t nc = chain_append(parent);
            if (nc == 0) return false;
            slot = 512;
        }
        start_cluster = alloc_cluster();
        if (start_cluster == 0) return false;
        FatDirEntry e;
        zbc(&e, sizeof(e));
        pack_name(leaf, e.name);
        e.attr = FAT_ATTR_ARCHIVE;
        e.cluster_lo = start_cluster;
        e.size = len;
        write_dir_slot(parent, slot, &e);
    }

    // 写数据:逐簇
    uint32_t bpc = bytes_per_cluster();
    uint16_t c = start_cluster;
    uint32_t done = 0;
    uint8_t csec[512];
    while (done < len) {
        uint32_t cbytes = bpc;
        if (done + cbytes > len) cbytes = len - done;
        // 写满整簇
        for (uint32_t s = 0; s < sectors_per_cluster_; s++) {
            zbc(csec, sizeof(csec));
            uint32_t secbase = done + s * 512;
            uint32_t chunk = 512;
            if (secbase + chunk > len) chunk = len - secbase;
            if ((int)chunk > 0) nefu::memcpy(csec, data + secbase, chunk);
            wr(cluster_to_lba(c) + s, csec);
        }
        done += cbytes;
        if (done < len) {
            uint16_t nc = chain_append(c);
            if (nc == 0) { free_chain(start_cluster); return false; }
            c = nc;
        }
    }
    if (len == 0) fat_set(start_cluster, FAT_EOF);

    // 更新目录项里的大小
    FatDirEntry e;
    long slot = -1;
    // 重新找槽并更新 size
    if (parent == 0) {
        uint8_t target[11]; pack_name(leaf, target);
        uint32_t slots = (uint32_t)root_entries_;
        uint8_t sec[512];
        for (uint32_t s = 0; s < slots; s++) {
            uint32_t lba = root_dir_lba() + s / 16;
            if ((s % 16) == 0) rd(lba, sec);
            FatDirEntry* ep = (FatDirEntry*)(sec + (s % 16) * 32);
            if (ep->name[0] == 0x00) break;
            if (memcmp(ep->name, target, 11) == 0) {
                ep->size = len;
                ep->cluster_lo = start_cluster;
                wr(lba, sec);
                break;
            }
        }
    }
    return true;
}

// ===================== 列目录 =====================
int Fat16::list_dir(const char* path, FatDirInfo* out, int max_out) {
    // 解析路径到父簇
    uint16_t parent;
    char leaf[32];
    // list_dir("/") 特殊处理
    uint16_t target = 0;
    if (nefu::strcmp(path, "/") == 0) {
        target = 0;
    } else {
        if (!resolve_parent(path, &parent, leaf, sizeof(leaf))) return 0;
        FatDirInfo info;
        if (!find_entry(parent, leaf, &info)) return 0;
        if (!info.is_dir) return 0;
        target = info.start_cluster;
    }

    int n = 0;
    uint8_t sec[512];
    if (target == 0) {
        uint32_t slots = (uint32_t)root_entries_;
        for (uint32_t s = 0; s < slots && n < max_out; s++) {
            uint32_t lba = root_dir_lba() + s / 16;
            if ((s % 16) == 0) rd(lba, sec);
            FatDirEntry* e = (FatDirEntry*)(sec + (s % 16) * 32);
            if (e->name[0] == 0x00) break;
            if (e->name[0] == 0xE5) continue;
            if (e->attr & FAT_ATTR_VOLLABEL) continue;
            unpack_name(e->name, out[n].name);
            out[n].is_dir = (e->attr & FAT_ATTR_DIR) != 0;
            out[n].is_deleted = false;
            out[n].start_cluster = (uint16_t)(e->cluster_hi | e->cluster_lo);
            out[n].size = e->size;
            n++;
        }
    } else {
        uint16_t c = target;
        while (c >= 2 && c < 0xFFF8 && n < max_out) {
            rd(cluster_to_lba(c), sec);
            for (int i = 0; i < 16 && n < max_out; i++) {
                FatDirEntry* e = (FatDirEntry*)(sec + i * 32);
                if (e->name[0] == 0x00) break;
                if (e->name[0] == 0xE5) continue;
                // 跳过 "." 和 ".."
                if (e->name[0] == '.' ) continue;
                unpack_name(e->name, out[n].name);
                out[n].is_dir = (e->attr & FAT_ATTR_DIR) != 0;
                out[n].is_deleted = false;
                out[n].start_cluster = (uint16_t)(e->cluster_hi | e->cluster_lo);
                out[n].size = e->size;
                n++;
            }
            c = fat_get(c);
        }
    }
    return n;
}

// ===================== 自检 =====================
int fat16_self_test() {
    int fails = 0;
    Disk disk;
    // 用一块 64MB 的小盘:64*2048 = 131072 扇区太大,改用 8192 扇区(4MB)
    if (!disk.create(8192)) return 1;

    Fat16 fs;
    // spc=8 => 4KB/簇
    if (!fs.format(&disk, 0, 8192, 8)) { return 2; }
    if (fs.total_clusters() < 100) fails++;

    // 写一个文件
    const char* msg = "Hello FAT16! This is a test of the nefu filesystem library.";
    uint32_t mlen = (uint32_t)nefu::strlen(msg);
    if (!fs.write_file("/hello.txt", (const uint8_t*)msg, mlen)) fails++;

    // 读回验证
    uint8_t rdbuf[256];
    int rd = fs.read_file("/hello.txt", rdbuf, sizeof(rdbuf));
    if (rd != (int)mlen) fails++;
    for (uint32_t i = 0; i < mlen; i++) if (rdbuf[i] != msg[i]) { fails++; break; }

    // 大小正确
    if (fs.file_size("/hello.txt") != (long)mlen) fails++;

    // 列根目录,应看到 hello.txt
    FatDirInfo entries[16];
    int n = fs.list_dir("/", entries, 16);
    if (n != 1) fails++;
    if (n >= 1) {
        if (nefu::strcmp(entries[0].name, "HELLO.TXT") != 0) fails++;
        if (entries[0].is_dir) fails++;
    }

    // 建目录
    if (!fs.mkdir("/docs")) fails++;
    // 在 docs 里写文件
    const char* doc = "document body line 1\nline 2\n";
    uint32_t dlen = (uint32_t)nefu::strlen(doc);
    if (!fs.write_file("/docs/note.txt", (const uint8_t*)doc, dlen)) fails++;
    // 读回
    uint8_t dbuf[128];
    int dr = fs.read_file("/docs/note.txt", dbuf, sizeof(dbuf));
    if (dr != (int)dlen) fails++;
    for (uint32_t i = 0; i < dlen; i++) if (dbuf[i] != doc[i]) { fails++; break; }

    // 列 /docs
    n = fs.list_dir("/docs", entries, 16);
    if (n != 1) fails++;
    if (n >= 1 && nefu::strcmp(entries[0].name, "NOTE.TXT") != 0) fails++;

    // 列根目录,应有 hello.txt 和 docs
    n = fs.list_dir("/", entries, 16);
    if (n != 2) fails++;

    // 删除文件
    if (!fs.remove("/hello.txt")) fails++;
    if (fs.file_size("/hello.txt") != -1) fails++;
    n = fs.list_dir("/", entries, 16);
    if (n != 1) fails++;

    // 重写一个大文件,跨多簇
    uint8_t big[10000];
    for (int i = 0; i < 10000; i++) big[i] = (uint8_t)(i & 0xFF);
    if (!fs.write_file("/big.dat", big, 10000)) fails++;
    uint8_t bigrd[10000];
    int br = fs.read_file("/big.dat", bigrd, sizeof(bigrd));
    if (br != 10000) fails++;
    for (int i = 0; i < 10000; i++) if (bigrd[i] != big[i]) { fails++; break; }

    // 删除大文件后空闲簇应回升
    uint32_t free_before = fs.free_clusters();
    if (!fs.remove("/big.dat")) fails++;
    uint32_t free_after = fs.free_clusters();
    if (free_after <= free_before) fails++;

    // 重命名
    if (!fs.write_file("/old.txt", (const uint8_t*)"rename me", 9)) fails++;
    if (!fs.rename("/old.txt", "new.txt")) fails++;
    if (fs.file_size("/old.txt") != -1) fails++;
    if (fs.file_size("/new.txt") != 9) fails++;

    // 截断
    if (!fs.truncate("/new.txt", 4)) fails++;
    if (fs.file_size("/new.txt") != 4) fails++;
    uint8_t trdb[16]; int trr = fs.read_file("/new.txt", trdb, sizeof(trdb));
    if (trr != 4) fails++;
    if (nefu::memcmp(trdb, "rena", 4) != 0) fails++;

    // stat
    FatDirInfo st;
    if (!fs.stat("/docs/note.txt", &st)) fails++;
    if (st.size != dlen) fails++;

    // 重新挂载,验证持久化
    Fat16 fs2;
    if (!fs2.mount(&disk, 0)) fails++;
    long sz = fs2.file_size("/docs/note.txt");
    if (sz != (long)dlen) fails++;

    // fsck
    Fat16::FsckReport rep;
    int prob = fs.fsck(&rep);
    if (prob != 0) fails++;
    if (rep.total_clusters != fs.total_clusters()) fails++;
    if (rep.free_clusters != fs.free_clusters()) fails++;
    if (rep.dir_entries < 2) fails++;

    // 多文件创建/删除压力
    uint32_t free_a = fs.free_clusters();
    for (int i = 0; i < 8; i++) {
        char nm[16]; nm[0]='T'; nm[1]=(char)('0'+i); nm[2]='.'; nm[3]='T'; nm[4]='X'; nm[5]='T'; nm[6]=0;
        char path[16]; path[0]='/'; path[1]=nm[0]; path[2]=nm[1]; path[3]='.'; path[4]='T'; path[5]='X'; path[6]='T'; path[7]=0;
        if (!fs.write_file(path,(const uint8_t*)"x",1)) { fails++; break; }
    }
    uint32_t free_b = fs.free_clusters();
    if (free_b >= free_a) fails++; // 应消耗簇
    for (int i = 0; i < 8; i++) {
        char path[16]; path[0]='/'; path[1]='T'; path[2]=(char)('0'+i); path[3]='.'; path[4]='T'; path[5]='X'; path[6]='T'; path[7]=0;
        fs.remove(path);
    }
    uint32_t free_c = fs.free_clusters();
    if (free_c < free_a - 5) fails++; // 基本回收

    return fails;
}

} // namespace filesystem
} // namespace nefu
