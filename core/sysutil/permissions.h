// nefuOS 系统工具扩展库 —— 权限管理模块
// 用户 / 组 / 权限位(rwx) / 访问控制列表(ACL) / sudo(模拟)。
// 全部为软件模拟，不依赖真实 Linux VFS；用于 sysmon 展示与权限策略自检。
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace sysutil {

// 权限位（9 位，低 9 位有效）
const int PERM_OTH_X = 1;
const int PERM_OTH_W = 2;
const int PERM_OTH_R = 4;
const int PERM_GRP_X = 8;
const int PERM_GRP_W = 16;
const int PERM_GRP_R = 32;
const int PERM_USR_X = 64;
const int PERM_USR_W = 128;
const int PERM_USR_R = 256;

// 请求的访问类型
enum AccessMode {
    ACCESS_READ = 4,
    ACCESS_WRITE = 2,
    ACCESS_EXEC = 1
};

struct SysUser {
    int   uid;
    int   gid;          // 主组
    char  name[24];
    char  home[40];
    bool  in_sudoers;   // 是否允许 sudo
};

struct SysGroup {
    int   gid;
    char  name[24];
    int   members[16];  // uid 列表
    int   member_count;
};

// ACL 条目：对某个 uid 或 gid 额外授予的权限位。
struct Ace {
    bool is_group;
    int  id;            // uid 或 gid
    int  mask;          // rwx 位
};

const int ACL_MAX = 8;
struct Acl {
    Ace entries[ACL_MAX];
    int count;
};

class PermissionManager {
public:
    PermissionManager();

    // 用户/组管理 -------------------------------------------------------
    int  add_user(const char* name, int gid);               // 返回 uid，-1 失败
    bool add_group(const char* name);                        // 返回 true
    bool group_add_member(int gid, int uid);
    int  find_user(const char* name) const;
    int  find_group(const char* name) const;
    const SysUser* user(int uid) const;
    const SysGroup* group(int gid) const;
    int  user_count() const { return users_.size(); }
    int  group_count() const { return groups_.size(); }

    // 访问控制 -----------------------------------------------------------
    // 标准 Unix 权限位判定：owner/group/other 三段。
    bool check(int uid, int owner_uid, int owner_gid, int mode, AccessMode req) const;
    // ACL 辅助判定：在标准位不够时，查 ACL 是否额外授权。
    bool check_acl(const Acl* acl, int uid, AccessMode req) const;
    // 组合判定：先标准位，再 ACL。
    bool check_full(int uid, int owner_uid, int owner_gid, int mode,
                    const Acl* acl, AccessMode req) const;

    // sudo（模拟）：user 名 + 口令是否允许提权到 root(uid=0)。
    // 这里用一个内置口令哈希表做演示，不做真实密码学。
    bool sudo_allow(const char* user, const char* password) const;
    // 给某用户设置 sudo 口令（明文存储仅为模拟演示）。
    void sudo_set_password(const char* user, const char* password);

    // 把 mode 格式化成 "rwxr-xr-x" 串。
    static void mode_string(int mode, char out[11]);
    // 反向解析 "rwxr-xr-x" 串成 mode。
    static int  parse_mode(const char* s);

    // umask：新建文件时从请求 mode 里屏蔽的位。
    void set_umask(int mask) { umask_ = mask & 0777; }
    int  get_umask() const { return umask_; }
    // 应用 umask 后的实际 mode：mode & ~umask
    int  apply_umask(int mode) const { return mode & ~umask_; }

    // ACL 操作：删除某 uid 的 ACL 条目。
    bool acl_remove(Acl* acl, int uid);
    // chmod：模拟修改文件权限位（这里只做位运算演示）。
    static int chmod(int mode, int add_mask, int remove_mask) {
        return (mode | add_mask) & ~remove_mask;
    }
    // 判断 uid 是否在 gid 组里（演示用：root=0 视为在所有组）。
    bool in_group(int uid, int gid) const;
    // 模拟文件表：登记一个路径的属主/属组/mode。
    struct FileNode { char path[40]; int owner; int group; int mode; };
    bool fs_register(const char* path, int owner, int group, int mode);
    // 查文件权限位。
    int  fs_mode(const char* path) const;
    // 导出某组的所有成员 uid 到 out，返回数量。
    int  group_members(int gid, int* out, int max) const;

    void reset();
    int self_test();

private:
    List<SysUser>  users_;
    List<SysGroup> groups_;
    // sudo 口令表（模拟）
    char sudo_user_[8][24];
    char sudo_pass_[8][24];
    int  sudo_count_;
    int  next_uid_;
    int  next_gid_;
    int  umask_;
    FileNode fs_[32];
    int      fs_count_;
};

// 访问模式名
const char* access_mode_name(AccessMode m);

extern PermissionManager g_perms;

} // namespace sysutil
} // namespace nefu
