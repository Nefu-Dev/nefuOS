// nefuOS 文件系统库 —— ext2 文件系统(简化仿真)
//
// 在虚拟磁盘上仿真经典 Unix ext2 的核心结构:
//
//   块大小 1024 字节(= 2 扇区),单块组布局:
//     块 0        引导保留(未用)
//     块 1        超级块(1024 字节,魔数 0xEF53)
//     块 2        块组描述符表(本仿真只用第 0 项)
//     块 3        块位图
//     块 4        inode 位图
//     块 5..      inode 表(每项 128 字节)
//     ...         数据块
//
// inode 128 字节,i_block[15]:12 直接块 + 1 间接块(双/三间接本仿真省略)。
// 目录项:4 字节 inode + 2 rec_len + 1 name_len + 1 file_type + 名字。
// 支持:格式化、挂载、创建文件/目录、读写、删除、符号链接、权限位。
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "disk.h"

namespace nefu {
namespace filesystem {

const uint16_t EXT2_MAGIC = 0xEF53;
const uint32_t EXT2_BLOCK_SIZE = 1024;
const uint32_t EXT2_SECTORS_PER_BLOCK = EXT2_BLOCK_SIZE / SECTOR_SIZE; // 2

// inode 模式位
const uint16_t EXT2_IFDIR  = 0x4000;  // 目录
const uint16_t EXT2_IFREG  = 0x8000;  // 普通文件
const uint16_t EXT2_IFLNK  = 0xA000;  // 符号链接
const uint16_t EXT2_IRWXU  = 0x1FF;   // 权限 rwxrwxrwx(简化)

// 超级块(磁盘上 1024 字节,这里只列关心的字段偏移)
struct Ext2Super {
    uint32_t inodes_count;
    uint32_t blocks_count;
    uint32_t r_blocks_count;
    uint32_t free_blocks_count;
    uint32_t free_inodes_count;
    uint32_t first_data_block;
    uint32_t log_block_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t mtime;
    uint32_t wtime;
    uint16_t mnt_count;
    uint16_t magic;
    uint16_t state;
    uint32_t incl_count;
    uint32_t block_group_nr;
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;
    uint8_t  volume_name[16];
};

// 块组描述符(32 字节)
struct Ext2GroupDesc {
    uint32_t block_bitmap;
    uint32_t inode_bitmap;
    uint32_t inode_table;
    uint16_t free_blocks_count;
    uint16_t free_inodes_count;
    uint16_t used_dirs_count;
    uint16_t pad;
    uint8_t  reserved[12];
};

// inode(128 字节)
struct Ext2Inode {
    uint16_t mode;
    uint16_t uid;
    uint32_t size;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t dtime;
    uint16_t gid;
    uint16_t links_count;
    uint32_t blocks;
    uint32_t flags;
    uint32_t osd1;
    uint32_t i_block[15];
    uint32_t version;
    uint32_t file_acl;
    uint32_t dir_acl;
    uint32_t fragment_addr;
    uint8_t  osd2[12];
};

// 目录项(磁盘上前 8 字节头 + 变长名)
struct Ext2DirHeader {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
};

// 内存里展开的目录项
struct Ext2DirInfo {
    char     name[64];
    bool     is_dir;
    bool     is_symlink;
    uint32_t inode;
    uint32_t size;
};

class Ext2 {
public:
    Ext2();
    ~Ext2();

    // 在 disk 的 start_lba 上格式化一个 ext2,共 total_lba 个扇区。
    bool format(Disk* disk, uint32_t start_lba, uint32_t total_lba);
    bool mount(Disk* disk, uint32_t start_lba);

    // ---- 路径解析 ----
    // 找绝对路径对应的 inode 号。根目录 / = inode 2。失败返回 0。
    uint32_t namei(const char* path);
    // 找父目录 inode + 末级名
    bool resolve_parent(const char* path, uint32_t* parent_inode, char* leaf, int leaf_sz);

    // ---- 文件操作 ----
    bool create_file(const char* path);
    bool mkdir(const char* path);
    bool remove(const char* path);
    bool write_file(const char* path, const uint8_t* data, uint32_t len);
    int  read_file(const char* path, uint8_t* buf, uint32_t bufsz);
    long file_size(const char* path);
    // 创建符号链接:linkpath -> target
    bool symlink(const char* target, const char* linkpath);
    // 读符号链接内容到 buf
    bool readlink(const char* path, char* buf, int bufsz);

    // 列目录
    int list_dir(const char* path, Ext2DirInfo* out, int max_out);

    // 重命名(同目录内)
    bool rename(const char* oldpath, const char* newname);
    // 统计目录条目数
    int  count_entries(const char* path);

    // ---- 权限 ----
    uint16_t get_mode(const char* path);
    bool set_mode(const char* path, uint16_t mode);

    // ---- 统计 ----
    uint32_t free_blocks();
    uint32_t total_blocks();
    uint32_t free_inodes();

    friend int ext2_self_test();

private:
    bool rd_block(uint32_t blk, uint8_t* buf);
    bool wr_block(uint32_t blk, const uint8_t* buf);
    bool rd_lba(uint32_t lba, uint8_t* buf);
    bool wr_lba(uint32_t lba, const uint8_t* buf);

    // 读写超级块/组描述符(带缓存的简单版本:每次都落盘)
    bool read_super(Ext2Super* s);
    bool write_super(const Ext2Super* s);
    bool read_gd(Ext2GroupDesc* g);
    bool write_gd(const Ext2GroupDesc* g);

    // 位图分配
    uint32_t alloc_block();
    void     free_block(uint32_t blk);
    uint32_t alloc_inode();
    void     free_inode(uint32_t ino);

    // inode 读写
    bool read_inode(uint32_t ino, Ext2Inode* in);
    bool write_inode(uint32_t ino, const Ext2Inode* in);

    // 把文件 logical block 号映射到物理块号(直接 + 单间接)
    uint32_t file_block_to_phys(Ext2Inode* in, uint32_t lb);
    // 确保文件有第 lb 个块(必要时分配),返回物理块号
    uint32_t ensure_block(Ext2Inode* in, uint32_t lb);

    // 目录操作:在 parent 目录 inode 下加一个目录项
    bool dir_add_entry(uint32_t parent_ino, const char* name, uint32_t target_ino, uint8_t file_type);
    // 在 parent 下找名字,返回 inode;失败 0
    uint32_t dir_lookup(uint32_t parent_ino, const char* name);
    // 删除一个目录项(不释放 inode/块)
    bool dir_remove_entry(uint32_t parent_ino, const char* name);

    Disk*    disk_;
    uint32_t part_start_;   // 分区起始 LBA(扇区)
    uint32_t total_blocks_; // 总块数
    uint32_t inodes_per_group_;
    uint32_t blocks_per_group_;
    bool     mounted_;
};

int ext2_self_test();

} // namespace filesystem
} // namespace nefu
