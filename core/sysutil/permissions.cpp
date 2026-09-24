// nefuOS 系统工具扩展库 —— 权限管理模块实现
#include "permissions.h"
#include "../klib/klib.h"

namespace nefu {
namespace sysutil {

PermissionManager g_perms;

namespace {
void copy_str(char* dst, int dstsz, const char* src) {
    if (!dst || dstsz <= 0) return;
    if (!src) src = "";
    int i = 0;
    for (; i < dstsz - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}
} // namespace

PermissionManager::PermissionManager() : next_uid_(1), next_gid_(1), sudo_count_(0), umask_(0022), fs_count_(0) {
    reset();
}

void PermissionManager::reset() {
    users_.clear();
    groups_.clear();
    sudo_count_ = 0;
    next_uid_ = 1;
    next_gid_ = 1;
    // 内置 root
    SysUser root;
    root.uid = 0; root.gid = 0;
    copy_str(root.name, sizeof(root.name), "root");
    copy_str(root.home, sizeof(root.home), "/root");
    root.in_sudoers = true;
    users_.push(root);
    SysGroup g0;
    g0.gid = 0; copy_str(g0.name, sizeof(g0.name), "root");
    g0.member_count = 0;
    groups_.push(g0);
    // 默认 sudo 口令 root/rootpass（仅模拟）
    sudo_set_password("root", "rootpass");
}

int PermissionManager::add_user(const char* name, int gid) {
    if (!name) return -1;
    SysUser u;
    u.uid = next_uid_++;
    u.gid = gid;
    copy_str(u.name, sizeof(u.name), name);
    copy_str(u.home, sizeof(u.home), "/home/");
    // 拼 /home/<name>
    int hl = (int)strlen("/home/");
    int nl = (int)strlen(name);
    if (hl + nl < (int)sizeof(u.home) - 1) {
        for (int i = 0; i < hl; i++) u.home[i] = "/home/"[i];
        for (int i = 0; i < nl; i++) u.home[hl + i] = name[i];
        u.home[hl + nl] = 0;
    }
    u.in_sudoers = false;
    users_.push(u);
    return u.uid;
}

bool PermissionManager::add_group(const char* name) {
    if (!name) return false;
    SysGroup g;
    g.gid = next_gid_++;
    copy_str(g.name, sizeof(g.name), name);
    g.member_count = 0;
    groups_.push(g);
    return true;
}

bool PermissionManager::group_add_member(int gid, int uid) {
    for (int i = 0; i < groups_.size(); i++) {
        if (groups_[i].gid == gid) {
            if (groups_[i].member_count >= 16) return false;
            groups_[i].members[groups_[i].member_count++] = uid;
            return true;
        }
    }
    return false;
}

int PermissionManager::find_user(const char* name) const {
    if (!name) return -1;
    for (int i = 0; i < users_.size(); i++)
        if (strcmp(users_[i].name, name) == 0) return users_[i].uid;
    return -1;
}

int PermissionManager::find_group(const char* name) const {
    if (!name) return -1;
    for (int i = 0; i < groups_.size(); i++)
        if (strcmp(groups_[i].name, name) == 0) return groups_[i].gid;
    return -1;
}

const SysUser* PermissionManager::user(int uid) const {
    for (int i = 0; i < users_.size(); i++)
        if (users_[i].uid == uid) return &users_[i];
    return 0;
}

const SysGroup* PermissionManager::group(int gid) const {
    for (int i = 0; i < groups_.size(); i++)
        if (groups_[i].gid == gid) return &groups_[i];
    return 0;
}

// Unix 标准权限位判定
bool PermissionManager::check(int uid, int owner_uid, int owner_gid,
                               int mode, AccessMode req) const {
    // root 一律放行
    if (uid == 0) return true;
    int bit = 0;
    switch (req) {
    case ACCESS_READ:  bit = PERM_OTH_R; break;
    case ACCESS_WRITE: bit = PERM_OTH_W; break;
    case ACCESS_EXEC:  bit = PERM_OTH_X; break;
    }
    if (uid == owner_uid) {
        int ub = bit << 6;   // owner 段在高 3 位
        return (mode & ub) != 0;
    }
    // 组匹配：用户主组或附加组等于 owner_gid
    bool in_group = false;
    const SysUser* u = user(uid);
    if (u && u->gid == owner_gid) in_group = true;
    if (!in_group) {
        for (int i = 0; i < groups_.size(); i++) {
            if (groups_[i].gid != owner_gid) continue;
            for (int j = 0; j < groups_[i].member_count; j++)
                if (groups_[i].members[j] == uid) { in_group = true; break; }
        }
    }
    if (in_group) {
        int gb = bit << 3;
        return (mode & gb) != 0;
    }
    return (mode & bit) != 0;
}

bool PermissionManager::check_acl(const Acl* acl, int uid, AccessMode req) const {
    if (!acl) return false;
    int bit = 0;
    switch (req) {
    case ACCESS_READ: bit = PERM_OTH_R; break;
    case ACCESS_WRITE: bit = PERM_OTH_W; break;
    case ACCESS_EXEC: bit = PERM_OTH_X; break;
    }
    for (int i = 0; i < acl->count; i++) {
        const Ace& e = acl->entries[i];
        if (!e.is_group && e.id == uid) {
            if (e.mask & bit) return true;
        }
    }
    return false;
}

bool PermissionManager::check_full(int uid, int owner_uid, int owner_gid,
                                    int mode, const Acl* acl, AccessMode req) const {
    if (check(uid, owner_uid, owner_gid, mode, req)) return true;
    if (acl && check_acl(acl, uid, req)) return true;
    return false;
}

bool PermissionManager::sudo_allow(const char* user, const char* password) const {
    if (!user || !password) return false;
    const SysUser* u = 0;
    for (int i = 0; i < users_.size(); i++) {
        if (strcmp(users_[i].name, user) == 0) { u = &users_[i]; break; }
    }
    if (!u || !u->in_sudoers) return false;
    for (int i = 0; i < sudo_count_; i++) {
        if (strcmp(sudo_user_[i], user) == 0 &&
            strcmp(sudo_pass_[i], password) == 0) return true;
    }
    return false;
}

void PermissionManager::sudo_set_password(const char* user, const char* password) {
    if (!user || !password) return;
    for (int i = 0; i < sudo_count_; i++) {
        if (strcmp(sudo_user_[i], user) == 0) {
            copy_str(sudo_pass_[i], 24, password);
            return;
        }
    }
    if (sudo_count_ >= 8) return;
    copy_str(sudo_user_[sudo_count_], 24, user);
    copy_str(sudo_pass_[sudo_count_], 24, password);
    sudo_count_++;
    // 把该用户标记为 sudoers
    for (int i = 0; i < users_.size(); i++) {
        if (strcmp(users_[i].name, user) == 0) users_[i].in_sudoers = true;
    }
}

int PermissionManager::group_members(int gid, int* out, int max) const {
    const SysGroup* g = group(gid);
    if (!g || !out) return 0;
    int n = g->member_count < max ? g->member_count : max;
    for (int i = 0; i < n; i++) out[i] = g->members[i];
    return n;
}
const char* access_mode_name(AccessMode m) {
    switch (m) {
    case ACCESS_READ:  return "READ";
    case ACCESS_WRITE: return "WRITE";
    case ACCESS_EXEC:  return "EXEC";
    }
    return "?";
}
bool PermissionManager::fs_register(const char* path, int owner, int group, int mode) {
    if (!path || fs_count_ >= 32) return false;
    FileNode& n = fs_[fs_count_++];
    int i = 0;
    for (; path[i] && i < 39; i++) n.path[i] = path[i];
    n.path[i] = 0;
    n.owner = owner; n.group = group; n.mode = mode;
    return true;
}

int PermissionManager::fs_mode(const char* path) const {
    for (int i = 0; i < fs_count_; i++)
        if (strcmp(fs_[i].path, path) == 0) return fs_[i].mode;
    return -1;
}
bool PermissionManager::in_group(int uid, int gid) const {
    if (uid == 0) return true;   // root 在所有组
    const SysGroup* g = group(gid);
    if (!g) return false;
    for (int i = 0; i < g->member_count; i++)
        if (g->members[i] == uid) return true;
    return false;
}
bool PermissionManager::acl_remove(Acl* acl, int uid) {
    if (!acl) return false;
    for (int i = 0; i < acl->count; i++) {
        if (!acl->entries[i].is_group && acl->entries[i].id == uid) {
            // 用最后一条覆盖
            acl->entries[i] = acl->entries[acl->count - 1];
            acl->count--;
            return true;
        }
    }
    return false;
}
int PermissionManager::parse_mode(const char* s) {
    if (!s) return 0;
    // 跳过开头的文件类型字符（如 'd'）
    if (*s == 'd' || *s == '-' || *s == 'l' || *s == 'c' || *s == 'b') s++;
    int mode = 0;
    for (int grp = 0; grp < 3; grp++) {
        int base = (grp == 0) ? 0400 : (grp == 1 ? 0040 : 0004);
        if (s[0] == 'r') mode |= base;
        if (s[1] == 'w') mode |= base / 2;
        if (s[2] == 'x') mode |= base / 4;
        s += 3;
    }
    return mode;
}
void PermissionManager::mode_string(int mode, char out[11]) {
    if (!out) return;
    const char* rwx = "rwx";
    // owner
    for (int b = 0; b < 3; b++) out[b]     = (mode & (PERM_USR_R >> b * 0)) ? rwx[0] : '-';
    // 上面写法不对，重写
    out[0] = (mode & 256) ? 'r' : '-';
    out[1] = (mode & 128) ? 'w' : '-';
    out[2] = (mode & 64)  ? 'x' : '-';
    out[3] = (mode & 32)  ? 'r' : '-';
    out[4] = (mode & 16)  ? 'w' : '-';
    out[5] = (mode & 8)   ? 'x' : '-';
    out[6] = (mode & 4)   ? 'r' : '-';
    out[7] = (mode & 2)   ? 'w' : '-';
    out[8] = (mode & 1)   ? 'x' : '-';
    out[9] = 0;
    (void)rwx;
}

// ---------------- 自检 ----------------
int PermissionManager::self_test() {
    int fails = 0;
    PermissionManager pm;
    pm.reset();

    int alice = pm.add_user("alice", 100);
    int bob   = pm.add_user("bob", 100);
    if (alice != 1 || bob != 2) fails++;
    int devg = pm.add_group("dev");
    if (devg < 0) fails++;
    pm.group_add_member(devg, alice);

    // 文件 owned by alice:640 (rw-r-----)
    int mode = PERM_USR_R | PERM_USR_W | PERM_GRP_R;
    // alice 自己读
    if (!pm.check(alice, alice, 100, mode, ACCESS_READ)) fails++;
    // alice 自己写
    if (!pm.check(alice, alice, 100, mode, ACCESS_WRITE)) fails++;
    // alice 自己执行：没有 x 位
    if (pm.check(alice, alice, 100, mode, ACCESS_EXEC)) fails++;
    // bob 不在 dev 组 -> other 段无读
    if (pm.check(bob, alice, devg, mode, ACCESS_READ)) fails++;
    // alice 在 dev 组 -> group 段有读
    if (!pm.check(alice, alice, devg, mode, ACCESS_READ)) fails++;
    // bob 写：group 段无 w
    if (pm.check(bob, alice, devg, mode, ACCESS_WRITE)) fails++;

    // root 万能
    if (!pm.check(0, alice, 100, 0, ACCESS_WRITE)) fails++;

    // mode_string
    char ms[11];
    PermissionManager::mode_string(mode, ms);
    if (strcmp(ms, "rw-r-----") != 0) fails++;
    PermissionManager::mode_string(0755, ms);
    if (strcmp(ms, "rwxr-xr-x") != 0) fails++;

    // ACL：给 bob 额外读权限
    Acl acl;
    acl.count = 1;
    acl.entries[0].is_group = false;
    acl.entries[0].id = bob;
    acl.entries[0].mask = PERM_OTH_R;
    // 标准位 bob 读不到
    if (pm.check(bob, alice, devg, mode, ACCESS_READ)) fails++;
    // ACL 补上
    if (!pm.check_full(bob, alice, devg, mode, &acl, ACCESS_READ)) fails++;

    // sudo
    pm.sudo_set_password("alice", "hunter2");
    if (!pm.sudo_allow("alice", "hunter2")) fails++;
    if (pm.sudo_allow("alice", "wrong")) fails++;
    if (pm.sudo_allow("bob", "anything")) fails++;   // bob 不在 sudoers

    // umask：0022 会屏蔽 group/other 写位
    pm.set_umask(0022);
    int m = pm.apply_umask(0666);
    if (m != 0644) fails++;        // rw-rw-rw- & ~(--w--w-) = rw-r--r--
    // chmod
    if (pm.chmod(0644, 0, 0600) != 0044) fails++;
    // acl_remove
    if (!pm.acl_remove(&acl, bob)) fails++;
    if (pm.acl_remove(&acl, bob)) fails++;   // 已删，再删失败
    if (acl.count != 0) fails++;

    // parse_mode 往返
    char mstr[11];
    PermissionManager::mode_string(0755, mstr);
    int back = PermissionManager::parse_mode(mstr);
    if (back != 0755) fails++;
    if (PermissionManager::parse_mode("rw-r--r--") != 0644) fails++;

    // fs_register / fs_mode
    if (!pm.fs_register("/etc/passwd", 0, 0, 0644)) fails++;
    if (pm.fs_mode("/etc/passwd") != 0644) fails++;
    if (pm.fs_mode("/nonexistent") != -1) fails++;

    // group_members
    int mbr[8];
    int nm = pm.group_members(devg, mbr, 8);
    if (nm < 1) fails++;

    // find
    if (pm.find_user("alice") != alice) fails++;
    if (pm.find_group("dev") != devg) fails++;

    if (nefu::strcmp(access_mode_name(ACCESS_EXEC), "EXEC") != 0) fails++;
    return fails;
}

} // namespace sysutil
} // namespace nefu
