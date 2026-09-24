// nefuOS 文件系统库 —— 虚拟文件系统层(VFS)
//
// VFS 在多种具体文件系统(FAT16 / ext2 / MinixFS)之上提供统一接口:
//   - 挂载点表:把不同文件系统实例挂到目录树上(如 FAT16 挂 /,ext2 挂 /home)
//   - 路径解析:把绝对路径映射到 (挂载点, 挂载内相对路径)
//   - 文件描述符表:open/read/write/close 的 POSIX 风格句柄
//   - 目录遍历:readdir 迭代
//
// 本层不直接读写磁盘,而是把请求转发给对应的具体 FS 实例。
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "disk.h"
#include "fat16.h"
#include "ext2.h"
#include "minixfs.h"

namespace nefu {
namespace filesystem {

// 支持挂载的文件系统类型
enum FsType {
    FS_TYPE_NONE = 0,
    FS_TYPE_FAT16,
    FS_TYPE_EXT2,
    FS_TYPE_MINIX
};

// 一个挂载点
struct MountPoint {
    char     prefix[64];     // 挂载路径,如 "/" 或 "/home"
    FsType   type;
    void*    fs;             // 指向 Fat16*/Ext2*/MinixFS*
    char     dev_name[32];   // 设备名(用于显示)
};

// 打开文件描述符
struct FsFile {
    bool     used;
    int      mount_idx;      // 属于哪个挂载点
    char     path[128];      // 挂载内路径
    uint32_t offset;         // 当前读写偏移
    uint32_t size;           // 文件大小
    bool     is_dir;
};

const int VFS_MAX_MOUNTS = 8;
const int VFS_MAX_FDS = 16;

class VfsLayer {
public:
    VfsLayer();
    ~VfsLayer();

    // 在 prefix 挂载一个已格式化好的 fs。prefix 必须以 '/' 开头。
    bool mount(const char* prefix, FsType type, void* fs, const char* dev_name);
    // 卸载
    bool umount(const char* prefix);

    // 把绝对路径拆成 (mount_idx, 挂载内相对路径)。返回挂载点索引,-1=失败。
    int  resolve(const char* abspath, char* inner_path, int inner_sz);

    // ---- 统一 POSIX 风格接口 ----
    int  open(const char* path);          // 返回 fd,-1=失败
    int  read(int fd, uint8_t* buf, uint32_t sz);
    int  write(int fd, const uint8_t* buf, uint32_t sz);
    int  close(int fd);
    long size(int fd);

    bool mkdir(const char* path);
    bool create(const char* path);
    bool remove(const char* path);
    bool rename(const char* oldpath, const char* newname);
    long file_size(const char* path);
    // 整体写文件(便捷)
    bool write_file(const char* path, const uint8_t* data, uint32_t len);
    // 列目录到 entries,返回条目数
    int  list_dir(const char* path, Ext2DirInfo* out, int max_out);

    // 挂载点信息
    int  mount_count() const { return mount_count_; }
    const MountPoint* get_mount(int i) const { return &mounts_[i]; }

    // 统计:所有挂载点总空闲块
    uint32_t total_free_blocks();

private:
    MountPoint mounts_[VFS_MAX_MOUNTS];
    int        mount_count_;
    FsFile     fds_[VFS_MAX_FDS];
};

int vfs_layer_self_test();

} // namespace filesystem
} // namespace nefu
