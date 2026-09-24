// =============================================================================
//  filecmd.h —— 文件操作命令 + 模块内"迷你内存文件系统(VFS)"
// -----------------------------------------------------------------------------
//  终端命令库不直接碰真实磁盘: 这里实现一棵自包含的内存目录树。
//  文件命令(touch/rm/mkdir/ls/tree/du/stat...)都作用在这棵树上。
//  文本命令(textcmd)也通过 vfs_read_file 读取这里的文件作为输入。
// =============================================================================
#pragma once

#include "termcmds_all.h"

namespace nefu {
namespace termcmds {

// 目录树节点
struct VfsNode {
    char     name[64];        // 节点名(不含路径)
    bool     is_dir;
    VfsNode* parent;
    nefu::List<VfsNode*> children;  // 子节点(目录)
    char*    data;            // 文件内容(目录为 0)
    int      size;            // 文件字节数
    uint32_t mode;            // 权限位(rwxr-xr-x 风格)
    uint32_t mtime;           // 修改时间(秒)
    uint32_t ino;             // 伪 inode 号
};

// ---- VFS 生命周期 ----
void     vfs_init();                 // 建空根目录 "/"
void     vfs_shutdown();             // 释放整棵树
VfsNode* vfs_root();

// ---- 路径解析 ----
//  支持绝对路径 "/a/b"、相对路径、"." 与 ".."。找不到返回 0。
VfsNode* vfs_resolve(const char* path);
//  解析父目录并返回末段名; 用于创建。失败返回 0。
VfsNode* vfs_resolve_parent(const char* path, char* out_name, int namecap);

// ---- 创建/删除 ----
VfsNode* vfs_mkdir(const char* path, bool create_parents);   // mkdir / mkdir -p
bool     vfs_rmdir(const char* path);                        // 只能删空目录
bool     vfs_remove(const char* path);                       // 递归删除(文件/目录)
VfsNode* vfs_touch(const char* path);                       // 建空文件(已存在则更新 mtime)
bool     vfs_write_file(const char* path, const char* data, int len); // 截断写
bool     vfs_read_file(const char* path, nefu::String& out);

// ---- 元数据 ----
bool     vfs_stat(const char* path, VfsNode& out_copy);
int      vfs_du(const char* path, uint64_t* out_bytes);     // 递归占用字节数
//  列目录: 把 dir 的子节点指针追加到 out(不递归)
int      vfs_list(const char* path, nefu::List<VfsNode*>& out);

// ---- 拷贝/移动 ----
bool     vfs_copy_tree(VfsNode* src, VfsNode* dst_parent, const char* new_name);
bool     vfs_cp(const char* src, const char* dst);
bool     vfs_mv(const char* src, const char* dst);

// ---- 命令入口(static, cpp 内) ----
//  int cmd_touch / cmd_rm / cmd_mkdir / ... 在 filecmd.cpp 中实现。

// ---- 测试 ----
int filecmd_self_test();

} // namespace termcmds
} // namespace nefu
