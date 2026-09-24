// nefuOS 文件系统库 —— FAT16 文件系统
//
// 在一块虚拟磁盘(Disk)之上实现经典的 FAT16 文件系统。布局:
//
//   扇区 0        引导扇区 / BPB(BIOS Parameter Block)
//   reserved..    FAT 表区(默认 2 份,每份 spf 扇区)
//   root..        根目录区(root_entries 个 32 字节目录项,固定大小)
//   data..        数据区,簇从 2 开始编号
//
// FAT16 用 16 位表项描述簇链:
//   0x0000   空闲簇
//   0xFFF7   坏簇
//   0xFFF8..0xFFFF 簇链结束(EOF)
//   其它值   下一个簇号
//
// 目录项 32 字节:8.3 文件名 + 属性 + 起始簇 + 文件大小。
// 本实现支持:格式化、挂载、创建/读/写/删除文件、建目录、列目录、长文件名近似。
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "disk.h"

namespace nefu {
namespace filesystem {

// FAT16 目录属性位
const uint8_t FAT_ATTR_RDONLY  = 0x01;
const uint8_t FAT_ATTR_HIDDEN  = 0x02;
const uint8_t FAT_ATTR_SYSTEM  = 0x04;
const uint8_t FAT_ATTR_VOLLABEL= 0x08;
const uint8_t FAT_ATTR_DIR     = 0x10;
const uint8_t FAT_ATTR_ARCHIVE = 0x20;

// FAT 特殊项值
const uint16_t FAT_FREE = 0x0000;
const uint16_t FAT_BAD  = 0xFFF7;
const uint16_t FAT_EOF  = 0xFFFF;   // 簇链结束标记(>=0xFFF8 都算 EOF)

// 一个目录项(32 字节,磁盘上的原始布局)
struct FatDirEntry {
    uint8_t  name[8];     // 8.3 主名,空格填充
    uint8_t  ext[3];      // 扩展名
    uint8_t  attr;        // 属性位
    uint8_t  reserved;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_hi;  // FAT16 里恒为 0
    uint16_t write_time;
    uint16_t write_date;
    uint16_t cluster_lo; // 起始簇号
    uint32_t size;        // 文件字节数
};

// 内存里展开的目录项(给上层用)
struct FatDirInfo {
    char     name[13];    // "NAME.EXT",NUL 结尾
    bool     is_dir;
    bool     is_deleted;
    uint16_t start_cluster;
    uint32_t size;
};

class Fat16 {
public:
    Fat16();
    ~Fat16();

    // 在 disk 的 start_lba 起、共 total_lba 个扇区上格式化一个 FAT16。
    // sectors_per_cluster 必须是 1/2/4/8/.../128。成功返回 true。
    bool format(Disk* disk, uint32_t start_lba, uint32_t total_lba,
                uint16_t sectors_per_cluster);

    // 挂载(读 BPB,验证魔数)。成功返回 true。
    bool mount(Disk* disk, uint32_t start_lba);

    // ---- 目录查找 ----
    // 在 parent 目录(簇号,0 表示根目录)下按 8.3 名查找。找到则填 out 返回 true。
    bool find_entry(uint16_t parent_cluster, const char* name, FatDirInfo* out);

    // ---- 文件/目录操作 ----
    // 创建一个空文件。已存在返回 false。
    bool create_file(const char* path);
    // 创建子目录。path 形如 "/dir" 或 "/dir/sub"。
    bool mkdir(const char* path);
    // 删除文件(释放簇链 + 标记目录项为已删除)。
    bool remove(const char* path);
    // 重命名(同目录内,newname 是新的 8.3 名)
    bool rename(const char* oldpath, const char* newname);
    // 截断文件到指定大小(缩小则释放尾部簇,放大则补零簇)
    bool truncate(const char* path, uint32_t new_size);
    // 读取文件属性
    bool stat(const char* path, FatDirInfo* out);

    // 写文件(整体覆盖):把 data[0..len) 写入 path,自动分配/释放簇。
    bool write_file(const char* path, const uint8_t* data, uint32_t len);
    // 读文件到 buf,最多 bufsz 字节,返回实际读到的字节数;-1=失败。
    int  read_file(const char* path, uint8_t* buf, uint32_t bufsz);
    // 获取文件大小,不存在返回 -1。
    long file_size(const char* path);

    // 列目录:把 parent 下的条目追加到 out。返回条目数。
    int  list_dir(const char* path, FatDirInfo* out, int max_out);

    // ---- 统计 ----
    uint32_t free_clusters();       // 空闲簇数
    uint32_t total_clusters();      // 总数据簇数
    uint16_t bytes_per_cluster();   // 每簇字节数

    // ---- 一致性检查(fsck 简化版)----
    struct FsckReport {
        uint32_t total_clusters;
        uint32_t free_clusters;
        uint32_t used_clusters;
        uint32_t bad_clusters;
        uint32_t dir_entries;
    };
    int fsck(FsckReport* out);

    // 自检需要直接戳 FAT
    friend int fat16_self_test();

private:
    // 从磁盘读/写一个扇区(带分区基址)
    bool rd(uint32_t lba, uint8_t* buf);
    bool wr(uint32_t lba, const uint8_t* buf);

    // FAT 表项读写
    uint16_t fat_get(uint16_t cluster);
    void     fat_set(uint16_t cluster, uint16_t value);

    // 簇号 -> 起始扇区号
    uint32_t cluster_to_lba(uint16_t cluster);
    // 分配一个空闲簇,返回簇号;无空间返回 0。
    uint16_t alloc_cluster();
    // 释放整条簇链
    void free_chain(uint16_t cluster);
    // 在链尾追加一个簇,返回新簇号(失败返回 0)
    uint16_t chain_append(uint16_t cluster);

    // 根目录扇区范围
    uint32_t root_dir_lba() const { return reserved_sectors_ + num_fats_ * sectors_per_fat_; }
    uint32_t root_dir_sectors() const {
        return (root_entries_ * 32 + 511) / 512;
    }
    uint32_t data_lba() const { return root_dir_lba() + root_dir_sectors(); }

    // 在指定目录簇下找一个空目录项槽,返回其字节偏移(相对于目录数据起始);
    // parent_cluster==0 表示根目录。找不到返回 -1。
    long find_empty_dir_slot(uint16_t parent_cluster);
    // 写一个目录项到指定槽(槽是"目录起始字节偏移")
    bool write_dir_slot(uint16_t parent_cluster, long slot_off, const FatDirEntry* e);
    bool read_dir_slot(uint16_t parent_cluster, long slot_off, FatDirEntry* e);

    // 把 "NAME.EXT" 8.3 名打包进 11 字节
    static void pack_name(const char* name, uint8_t out[11]);
    // 把 11 字节解成 "NAME.EXT"
    static void unpack_name(const uint8_t in[11], char out[13]);

    // 路径解析:找到父目录簇与最后一级名字。"/a/b.txt" -> parent_cluster, name="b.txt"
    // 根目录 "/" -> parent_cluster=0。失败返回 false。
    bool resolve_parent(const char* path, uint16_t* parent_cluster, char* name_out, int name_sz);

    Disk*    disk_;
    uint32_t part_start_;       // 分区在整盘上的起始 LBA

    // BPB 字段
    uint16_t bytes_per_sector_; // 恒 512
    uint8_t  sectors_per_cluster_;
    uint16_t reserved_sectors_;
    uint8_t  num_fats_;
    uint16_t root_entries_;
    uint16_t total_sectors_;
    uint8_t  media_descriptor_;
    uint16_t sectors_per_fat_;
};

// 模块自检
int fat16_self_test();

} // namespace filesystem
} // namespace nefu
