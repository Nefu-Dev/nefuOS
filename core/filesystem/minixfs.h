// nefuOS 文件系统库 —— Minix 文件系统(V1/V2 仿真)
//
// 在虚拟磁盘上仿真经典 Minix 文件系统:
//
//   块 0     引导块(未用)
//   块 1     超级块
//   块 2..   inode 位图(imap_blocks 块)
//   接着     块位图(zmap_blocks 块)
//   接着     inode 表
//   接着     数据区(zones)
//
// V1:16 位 zone 号;V2:32 位 zone 号。inode 含 7 直接 + 1 间接 + 1 双间接。
// 目录项:V1 = 2B inode + 14B 名(16B);V2 = 4B inode + 28B 名(32B)。
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "disk.h"

namespace nefu {
namespace filesystem {

const uint16_t MINIX_V1_MAGIC = 0x137F;
const uint16_t MINIX_V2_MAGIC = 0x2468;

// inode 模式
const uint16_t MINIX_TYPE_MASK = 0xF000;
const uint16_t MINIX_REG  = 0x8000;
const uint16_t MINIX_DIR  = 0x4000;
const uint16_t MINIX_SYM  = 0xA000;

// 超级块(块 1,共 1024 字节,这里列关心字段)
struct MinixSuper {
    uint16_t ninodes;       // V1: 16 位
    uint16_t nzones;        // V1: 16 位 zone 数
    uint16_t imap_blocks;
    uint16_t zmap_blocks;
    uint16_t firstdatazone;
    uint16_t log_zone_size;
    uint32_t max_size;
    uint16_t magic;
    uint16_t state;
};

// 磁盘 inode(64 字节,V1)
struct MinixInode {
    uint16_t mode;
    uint16_t uid;
    uint32_t size;
    uint32_t time;
    uint8_t  gid;
    uint8_t  nlinks;
    uint16_t zone[9];   // 7 direct + 1 indirect + 1 double(V1 16 位)
};

// 内存里展开的目录项
struct MinixDirInfo {
    char     name[30];
    bool     is_dir;
    uint32_t inode;
    uint32_t size;
};

class MinixFS {
public:
    MinixFS();
    ~MinixFS();

    // format_version:1=V1(16位),2=V2(32位)
    bool format(Disk* disk, uint32_t start_lba, uint32_t total_lba, int version);
    bool mount(Disk* disk, uint32_t start_lba);

    uint32_t namei(const char* path);
    bool create_file(const char* path);
    bool mkdir(const char* path);
    bool remove(const char* path);
    bool rename(const char* oldpath, const char* newname);
    bool write_file(const char* path, const uint8_t* data, uint32_t len);
    int  read_file(const char* path, uint8_t* buf, uint32_t bufsz);
    long file_size(const char* path);
    int  list_dir(const char* path, MinixDirInfo* out, int max_out);

    uint32_t free_zones();
    uint32_t total_zones();
    int  version() const { return version_; }

    friend int minixfs_self_test();

private:
    bool rd_block(uint32_t blk, uint8_t* buf);
    bool wr_block(uint32_t blk, const uint8_t* buf);
    bool rd_lba(uint32_t lba, uint8_t* buf);
    bool wr_lba(uint32_t lba, const uint8_t* buf);

    bool read_super(MinixSuper* s);
    bool write_super(const MinixSuper* s);

    uint32_t alloc_zone();
    void     free_zone(uint32_t z);
    uint32_t alloc_inode();
    void     free_inode(uint32_t ino);

    bool read_inode(uint32_t ino, MinixInode* in);
    bool write_inode(uint32_t ino, const MinixInode* in);

    // zone 号读写(V1=16位 / V2=32位)
    uint32_t read_zone_field(const uint8_t* p);
    void     write_zone_field(uint8_t* p, uint32_t z);

    uint32_t file_zone_to_phys(MinixInode* in, uint32_t lb);
    uint32_t ensure_zone(MinixInode* in, uint32_t lb);

    bool dir_add_entry(uint32_t parent, const char* name, uint32_t ino);
    uint32_t dir_lookup(uint32_t parent, const char* name);
    bool dir_remove_entry(uint32_t parent, const char* name);
    bool resolve_parent(const char* path, uint32_t* parent, char* leaf, int leaf_sz);

    // 布局计算
    uint32_t inode_blocks() { return (ninodes_ * 64) / 1024; }
    uint32_t inode_table_blk() { return 2 + imap_blocks_ + zmap_blocks_; }
    uint32_t first_data_blk() { return inode_table_blk() + inode_blocks(); }

    Disk*    disk_;
    uint32_t part_start_;
    int      version_;       // 1 or 2
    uint16_t ninodes_;
    uint16_t nzones_;
    uint16_t imap_blocks_;
    uint16_t zmap_blocks_;
    uint16_t firstdatazone_;
    uint32_t total_blocks_;
};

int minixfs_self_test();

} // namespace filesystem
} // namespace nefu
