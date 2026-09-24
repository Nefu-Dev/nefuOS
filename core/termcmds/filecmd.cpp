// =============================================================================
//  filecmd.cpp —— 文件操作命令 + 迷你内存 VFS 实现
// =============================================================================
#include "filecmd.h"

namespace nefu {
namespace termcmds {

// -----------------------------------------------------------------------------
//  内部全局: 根节点
// -----------------------------------------------------------------------------
static VfsNode* g_root = 0;
static uint32_t g_next_ino = 1;

// 手动清零(规避 MinGW -O2 memset 误优化)
static void node_zero(VfsNode* n) {
    // 不依赖 memset,逐字段手工置零
    for (int i = 0; i < 64; i++) n->name[i] = 0;
    n->is_dir = false;
    n->parent = 0;
    // children 是 List,自带默认构造,无需手清
    n->data = 0;
    n->size = 0;
    n->mode = 0;
    n->mtime = 0;
    n->ino = 0;
}

VfsNode* vfs_root() { return g_root; }

void vfs_init() {
    if (g_root) return;
    g_root = new VfsNode();
    node_zero(g_root);
    nefu::strcpy(g_root->name, "/");
    g_root->is_dir = true;
    g_root->parent = 0;
    g_root->mode = 0755;
    g_root->ino = g_next_ino++;
    g_root->mtime = g_term_now_sec ? g_term_now_sec() : 0;
}

// 递归释放一棵子树
static void free_tree(VfsNode* n) {
    if (!n) return;
    for (int i = 0; i < n->children.size(); i++) free_tree(n->children[i]);
    if (n->data) delete[] n->data;
    n->children.erase_all();
    delete n;
}

void vfs_shutdown() {
    if (g_root) { free_tree(g_root); g_root = 0; }
}

// -----------------------------------------------------------------------------
//  路径工具
// -----------------------------------------------------------------------------
//  在 parent 的孩子里按名找节点
static VfsNode* child_find(VfsNode* parent, const char* name) {
    if (!parent || !parent->is_dir) return 0;
    for (int i = 0; i < parent->children.size(); i++) {
        if (nefu::strcmp(parent->children[i]->name, name) == 0) return parent->children[i];
    }
    return 0;
}

VfsNode* vfs_resolve(const char* path) {
    if (!path || !g_root) return 0;
    VfsNode* cur = g_root;
    // 绝对路径从根开始; 相对路径也从根(终端里 cwd 固定为 /,简化)
    const char* p = path;
    while (*p == '/') p++;
    char comp[128];
    while (*p) {
        int i = 0;
        while (*p && *p != '/') {
            if (i < 127) comp[i++] = *p;
            p++;
        }
        comp[i] = 0;
        while (*p == '/') p++;
        if (i == 0) continue;
        if (nefu::strcmp(comp, ".") == 0) continue;
        if (nefu::strcmp(comp, "..") == 0) {
            if (cur->parent) cur = cur->parent;
            continue;
        }
        VfsNode* n = child_find(cur, comp);
        if (!n) return 0;
        cur = n;
    }
    return cur;
}

VfsNode* vfs_resolve_parent(const char* path, char* out_name, int namecap) {
    if (!path) return 0;
    // 找最后一个 '/',把路径切成 dirpart + name
    const char* slash = 0;
    for (const char* p = path; *p; p++) if (*p == '/') slash = p;
    if (!slash) {
        // 相对当前根
        nefu::strncpy(out_name, path, namecap - 1); out_name[namecap - 1] = 0;
        return g_root;
    }
    int dirlen = (int)(slash - path);
    char dpath[256];
    if (dirlen == 0) { dpath[0] = '/'; dpath[1] = 0; }
    else {
        if (dirlen > 255) dirlen = 255;
        for (int i = 0; i < dirlen; i++) dpath[i] = path[i];
        dpath[dirlen] = 0;
    }
    const char* nm = slash + 1;
    nefu::strncpy(out_name, nm, namecap - 1); out_name[namecap - 1] = 0;
    if (dpath[0] == '/' && dpath[1] == 0) return g_root;
    return vfs_resolve(dpath);
}

static VfsNode* node_create(VfsNode* parent, const char* name, bool is_dir) {
    if (!parent || !parent->is_dir) return 0;
    VfsNode* n = new VfsNode();
    node_zero(n);
    nefu::strncpy(n->name, name, 63); n->name[63] = 0;
    n->is_dir = is_dir;
    n->parent = parent;
    n->mode = is_dir ? 0755 : 0644;
    n->ino = g_next_ino++;
    n->mtime = g_term_now_sec ? g_term_now_sec() : 0;
    parent->children.push(n);
    return n;
}

VfsNode* vfs_mkdir(const char* path, bool create_parents) {
    if (!g_root) vfs_init();
    if (!path || !*path) return 0;
    // 逐段创建
    VfsNode* cur = g_root;
    const char* p = path;
    while (*p == '/') p++;
    char comp[128];
    while (*p) {
        int i = 0;
        while (*p && *p != '/') { if (i < 127) comp[i++] = *p; p++; }
        comp[i] = 0;
        while (*p == '/') p++;
        if (i == 0) continue;
        if (nefu::strcmp(comp, ".") == 0) continue;
        if (nefu::strcmp(comp, "..") == 0) { if (cur->parent) cur = cur->parent; continue; }
        VfsNode* n = child_find(cur, comp);
        if (!n) {
            if (!create_parents) return 0;
            n = node_create(cur, comp, true);
            if (!n) return 0;
        } else if (!n->is_dir) {
            return 0; // 名字已被文件占用
        }
        cur = n;
    }
    return cur;
}

VfsNode* vfs_touch(const char* path) {
    if (!g_root) vfs_init();
    if (!path || !*path) return 0;
    VfsNode* ex = vfs_resolve(path);
    if (ex) { ex->mtime = g_term_now_sec ? g_term_now_sec() : 0; return ex; }
    char name[128];
    VfsNode* parent = vfs_resolve_parent(path, name, sizeof(name));
    if (!parent || !*name) return 0;
    return node_create(parent, name, false);
}

bool vfs_write_file(const char* path, const char* data, int len) {
    VfsNode* n = vfs_resolve(path);
    if (!n) n = vfs_touch(path);
    if (!n || n->is_dir) return false;
    if (n->data) { delete[] n->data; n->data = 0; n->size = 0; }
    if (len > 0) {
        n->data = new char[len + 1];
        // 手动拷贝(避免任何优化疑虑)
        for (int i = 0; i < len; i++) n->data[i] = data[i];
        n->data[len] = 0;
        n->size = len;
    }
    n->mtime = g_term_now_sec ? g_term_now_sec() : 0;
    return true;
}

bool vfs_read_file(const char* path, nefu::String& out) {
    VfsNode* n = vfs_resolve(path);
    if (!n || n->is_dir || !n->data) return false;
    out = nefu::String(n->data, n->size);
    return true;
}

static void remove_child(VfsNode* parent, VfsNode* child) {
    for (int i = 0; i < parent->children.size(); i++) {
        if (parent->children[i] == child) { parent->children.remove(i); return; }
    }
}

bool vfs_remove(const char* path) {
    VfsNode* n = vfs_resolve(path);
    if (!n || n == g_root) return false;
    remove_child(n->parent, n);
    free_tree(n);
    return true;
}

bool vfs_rmdir(const char* path) {
    VfsNode* n = vfs_resolve(path);
    if (!n || !n->is_dir) return false;
    if (n->children.size() != 0) return false; // 非空
    remove_child(n->parent, n);
    delete n;
    return true;
}

bool vfs_stat(const char* path, VfsNode& out_copy) {
    VfsNode* n = vfs_resolve(path);
    if (!n) return false;
    out_copy = *n;
    return true;
}

int vfs_list(const char* path, nefu::List<VfsNode*>& out) {
    VfsNode* n = vfs_resolve(path ? path : "/");
    if (!n || !n->is_dir) return 0;
    for (int i = 0; i < n->children.size(); i++) out.push(n->children[i]);
    return out.size();
}

int vfs_du(const char* path, uint64_t* out_bytes) {
    VfsNode* n = vfs_resolve(path);
    if (!n) return -1;
    uint64_t total = 0;
    // 递归累加
    nefu::List<VfsNode*> stack;
    stack.push(n);
    while (!stack.empty()) {
        VfsNode* cur = stack.pop();
        if (!cur->is_dir) total += (uint64_t)cur->size;
        else for (int i = 0; i < cur->children.size(); i++) stack.push(cur->children[i]);
    }
    if (out_bytes) *out_bytes = total;
    return 0;
}

// 深拷贝一棵子树到 dst_parent 下,新名字 new_name
bool vfs_copy_tree(VfsNode* src, VfsNode* dst_parent, const char* new_name) {
    if (!src || !dst_parent || !dst_parent->is_dir) return false;
    VfsNode* dup = node_create(dst_parent, new_name ? new_name : src->name, src->is_dir);
    if (!dup) return false;
    if (!src->is_dir) {
        if (src->size > 0) {
            dup->data = new char[src->size + 1];
            for (int i = 0; i < src->size; i++) dup->data[i] = src->data[i];
            dup->data[src->size] = 0;
            dup->size = src->size;
        }
    } else {
        for (int i = 0; i < src->children.size(); i++) {
            vfs_copy_tree(src->children[i], dup, src->children[i]->name);
        }
    }
    return true;
}

bool vfs_cp(const char* src, const char* dst) {
    VfsNode* s = vfs_resolve(src);
    if (!s) return false;
    VfsNode* d = vfs_resolve(dst);
    if (d && d->is_dir) {
        // 目标是目录: 放到其下,保持原名
        return vfs_copy_tree(s, d, s->name);
    }
    char name[128];
    VfsNode* dpar = vfs_resolve_parent(dst, name, sizeof(name));
    if (!dpar || !*name) return false;
    // 若目标父目录已存在同名节点,先删掉它(覆盖语义)
    VfsNode* ex = child_find(dpar, name);
    if (ex) {
        remove_child(dpar, ex);
        free_tree(ex);
    }
    return vfs_copy_tree(s, dpar, name);
}

bool vfs_mv(const char* src, const char* dst) {
    VfsNode* s = vfs_resolve(src);
    if (!s || s == g_root) return false;
    VfsNode* d = vfs_resolve(dst);
    if (d == s) return false;
    if (d && d->is_dir) {
        // 移动进目录
        remove_child(s->parent, s);
        s->parent = d;
        d->children.push(s);
        return true;
    }
    char name[128];
    VfsNode* dpar = vfs_resolve_parent(dst, name, sizeof(name));
    if (!dpar || !*name) return false;
    remove_child(s->parent, s);
    // 改名
    for (int i = 0; i < 63 && name[i]; i++) s->name[i] = name[i];
    s->name[nefu::strlen(name) > 63 ? 63 : (int)nefu::strlen(name)] = 0;
    s->parent = dpar;
    dpar->children.push(s);
    return true;
}

// -----------------------------------------------------------------------------
//  命令实现
// -----------------------------------------------------------------------------
//  权限位 -> "drwxr-xr-x" 风格字符串
static void mode_to_str(uint32_t m, bool is_dir, char* out) {
    out[0] = is_dir ? 'd' : '-';
    const char* rwx = "rwx";
    for (int g = 0; g < 3; g++) {
        int shift = (int)(9 - g * 3 - 3);
        for (int b = 0; b < 3; b++) {
            out[1 + g * 3 + b] = (m & (1 << (shift + b))) ? rwx[b] : '-';
        }
    }
    out[10] = 0;
}

static int cmd_touch(int argc, const char** argv, TermOutput* out) {
    if (argc < 2) { out->pln("usage: touch <file...>"); return 1; }
    for (int i = 1; i < argc; i++) {
        if (!vfs_touch(argv[i])) out->pfln("touch: cannot create %s", argv[i]);
    }
    return 0;
}

static int cmd_mkdir(int argc, const char** argv, TermOutput* out) {
    bool parents = false;
    int start = 1;
    if (argc > 1 && nefu::strcmp(argv[1], "-p") == 0) { parents = true; start = 2; }
    if (start >= argc) { out->pln("usage: mkdir [-p] <dir...>"); return 1; }
    for (int i = start; i < argc; i++) {
        if (!vfs_mkdir(argv[i], parents)) out->pfln("mkdir: cannot create %s", argv[i]);
    }
    return 0;
}

static int cmd_rmdir(int argc, const char** argv, TermOutput* out) {
    if (argc < 2) { out->pln("usage: rmdir <dir...>"); return 1; }
    int bad = 0;
    for (int i = 1; i < argc; i++) {
        if (!vfs_rmdir(argv[i])) { out->pfln("rmdir: %s: not empty or missing", argv[i]); bad++; }
    }
    return bad ? 1 : 0;
}

static int cmd_rm(int argc, const char** argv, TermOutput* out) {
    bool force = false;
    int start = 1;
    if (argc > 1 && nefu::strcmp(argv[1], "-r") == 0) { start = 2; }
    if (argc > start && nefu::strcmp(argv[start], "-f") == 0) { force = true; start++; }
    if (start >= argc) { out->pln("usage: rm [-r] [-f] <path...>"); return 1; }
    for (int i = start; i < argc; i++) {
        if (!vfs_remove(argv[i]) && !force)
            out->pfln("rm: cannot remove %s", argv[i]);
    }
    return 0;
}

static int cmd_cp(int argc, const char** argv, TermOutput* out) {
    if (argc != 3) { out->pln("usage: cp <src> <dst>"); return 1; }
    if (!vfs_cp(argv[1], argv[2])) out->pfln("cp: %s -> %s failed", argv[1], argv[2]);
    return 0;
}

static int cmd_mv(int argc, const char** argv, TermOutput* out) {
    if (argc != 3) { out->pln("usage: mv <src> <dst>"); return 1; }
    if (!vfs_mv(argv[1], argv[2])) out->pfln("mv: %s -> %s failed", argv[1], argv[2]);
    return 0;
}

// ls: 支持 -l / -a / -la
static int cmd_ls(int argc, const char** argv, TermOutput* out) {
    bool long_fmt = false, all = false;
    int start = 1;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (const char* f = argv[i] + 1; *f; f++) {
                if (*f == 'l') long_fmt = true;
                if (*f == 'a') all = true;
            }
            start++;
        } else break;
    }
    const char* target = (start < argc) ? argv[start] : "/";
    VfsNode* n = vfs_resolve(target);
    if (!n) { out->pfln("ls: cannot access %s", target); return 1; }
    if (!n->is_dir) {
        // 单文件
        if (long_fmt) {
            char mode[12]; mode_to_str(n->mode, false, mode);
            out->pfln("%s %5d %s", mode, n->size, n->name);
        } else out->pln(n->name);
        return 0;
    }
    nefu::List<VfsNode*> kids;
    vfs_list(target, kids);
    if (long_fmt) out->pln("mode    size  name");
    for (int i = 0; i < kids.size(); i++) {
        VfsNode* k = kids[i];
        if (!all && k->name[0] == '.') continue;
        if (long_fmt) {
            char mode[12]; mode_to_str(k->mode, k->is_dir, mode);
            out->pfln("%s %6d  %s%s", mode, k->size, k->name, k->is_dir ? "/" : "");
        } else out->pln(k->name);
    }
    return 0;
}

// tree: 递归打印
static void tree_walk(VfsNode* n, const char* prefix, bool last, TermOutput* out, int depth, int maxdepth) {
    char line[256];
    nefu::String p = prefix;
    p += last ? "`-- " : "|-- ";
    p += n->name;
    if (n->is_dir) p += "/";
    out->pln(p.c_str());
    if (n->is_dir && depth < maxdepth) {
        nefu::String child_prefix = prefix;
        child_prefix += last ? "    " : "|   ";
        for (int i = 0; i < n->children.size(); i++) {
            tree_walk(n->children[i], child_prefix.c_str(), i == n->children.size() - 1, out, depth + 1, maxdepth);
        }
    }
}

static int cmd_tree(int argc, const char** argv, TermOutput* out) {
    const char* target = (argc > 1 && argv[1][0] != '-') ? argv[1] : "/";
    VfsNode* n = vfs_resolve(target);
    if (!n) { out->pfln("tree: %s not found", target); return 1; }
    out->pln(n->name);
    for (int i = 0; i < n->children.size(); i++) {
        tree_walk(n->children[i], "", i == n->children.size() - 1, out, 1, 64);
    }
    return 0;
}

static int cmd_du(int argc, const char** argv, TermOutput* out) {
    const char* target = (argc > 1) ? argv[1] : ".";
    uint64_t bytes = 0;
    if (vfs_du(target, &bytes) != 0) { out->pfln("du: cannot access %s", target); return 1; }
    out->pfln("%u\t%s", (uint32_t)bytes, target);
    return 0;
}

static int cmd_stat(int argc, const char** argv, TermOutput* out) {
    if (argc != 2) { out->pln("usage: stat <file>"); return 1; }
    VfsNode info;
    if (!vfs_stat(argv[1], info)) { out->pfln("stat: cannot stat %s", argv[1]); return 1; }
    char mode[12]; mode_to_str(info.mode, info.is_dir, mode);
    out->pfln("  File: %s", info.name);
    out->pfln("  Size: %d", info.size);
    out->pfln("Blocks: %u", (uint32_t)((info.size + 511) / 512));
    out->pfln("FileType: %s", info.is_dir ? "directory" : "regular file");
    out->pfln("Permissions: (%s/0%03o)", mode, info.mode & 0777);
    out->pfln("Inode: %u", info.ino);
    return 0;
}

// -----------------------------------------------------------------------------
//  self_test
// -----------------------------------------------------------------------------
int filecmd_self_test() {
    int fails = 0;
    vfs_init();
    // 清空根(保证测试独立)
    while (g_root->children.size()) vfs_remove(g_root->children[0]->name);

    // 1) touch + write + read
    {
        const char* av[2] = {"touch", "/hello.txt"};
        BufferTermOutput b; TermOutput o = b.out();
        int r = cmd_touch(2, av, &o);
        if (r != 0) fails++;
        if (!vfs_write_file("/hello.txt", "nefu", 4)) fails++;
        nefu::String rd;
        if (!vfs_read_file("/hello.txt", rd) || rd != "nefu") fails++;
    }
    // 2) mkdir -p
    {
        const char* av[4] = {"mkdir", "-p", "/a/b/c", "/a/x"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_mkdir(4, av, &o);
        if (!vfs_resolve("/a/b/c")) fails++;
        if (!vfs_resolve("/a/x")) fails++;
    }
    // 3) ls
    {
        vfs_write_file("/a/x/f1.txt", "AAA", 3);
        vfs_write_file("/a/x/f2.txt", "BBBBBB", 6);
        const char* av[3] = {"ls", "-l", "/a/x"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_ls(3, av, &o);
        if (!b.contains("f1.txt") || !b.contains("f2.txt")) fails++;
        if (!b.contains("3")) fails++;   // f1 size
    }
    // 4) cp 内容一致
    {
        const char* av[3] = {"cp", "/hello.txt", "/hello.bak"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_cp(3, av, &o);
        nefu::String rd;
        if (!vfs_read_file("/hello.bak", rd) || rd != "nefu") fails++;
    }
    // 5) mv 改名
    {
        const char* av[3] = {"mv", "/hello.bak", "/hello2.txt"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_mv(3, av, &o);
        if (vfs_resolve("/hello.bak")) fails++;
        if (!vfs_resolve("/hello2.txt")) fails++;
    }
    // 6) du 递归大小
    {
        uint64_t bytes = 0;
        vfs_du("/a", &bytes);
        // f1=3, f2=6 => 9
        if (bytes != 9) fails++;
    }
    // 7) stat
    {
        const char* av[2] = {"stat", "/hello2.txt"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_stat(2, av, &o);
        if (!b.contains("Size: 4")) fails++;
    }
    // 8) rm -r 递归
    {
        const char* av[3] = {"rm", "-r", "/a"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_rm(3, av, &o);
        if (vfs_resolve("/a")) fails++;
    }
    // 9) rmdir 拒绝非空
    {
        vfs_mkdir("/d1", true);
        vfs_write_file("/d1/f", "x", 1);
        const char* av[2] = {"rmdir", "/d1"};
        BufferTermOutput b; TermOutput o = b.out();
        int r = cmd_rmdir(2, av, &o);
        if (r == 0) fails++;   // 应当失败
        vfs_remove("/d1");
    }
    // 10) glob(归在 all 里,这里顺带验)
    if (!glob_match("*.txt", "readme.txt")) fails++;
    if (glob_match("*.txt", "readme.md")) fails++;
    if (!glob_match("a*b", "axb")) fails++;


    vfs_shutdown();
    return fails;
}

} // namespace termcmds
} // namespace nefu
