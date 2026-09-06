// nefuOS 虚拟文件树实现
#include "vfs.h"
#include "../platform.h"

namespace nefu {

VFS* g_vfs = 0;

VFS::VFS() {
    root_.name = "/";
    root_.is_dir = true;
    root_.parent = 0;
    root_.expanded = true;
    cwd = &root_;
}

VFS::~VFS() {
    free_tree(&root_);
    root_.children.erase_all();
}

FSNode* VFS::alloc_node() {
    FSNode* n = new FSNode();
    if (!n) return 0;
    return n;
}

void VFS::free_tree(FSNode* n) {
    if (!n) return;
    for (int i = 0; i < n->children.size(); i++) free_tree(n->children[i]);
    if (n->data) { kfree(n->data); n->data = 0; }
    if (n != &root_) delete n;
}

void VFS::split_path(const char* path, List<String>& parts) {
    if (!path) return;
    int i = 0;
    while (path[i]) {
        while (path[i] == '/') i++;
        int start = i;
        while (path[i] && path[i] != '/') i++;
        if (i > start) {
            String seg;
            for (int j = start; j < i; j++) seg += path[j];
            parts.push(seg);
        }
    }
}

FSNode* VFS::find_child(FSNode* d, const char* name) {
    for (int i = 0; i < d->children.size(); i++) {
        if (d->children[i]->name == name) return d->children[i];
    }
    return 0;
}

FSNode* VFS::resolve_from(FSNode* base, const char* path) {
    if (!base || !path) return 0;
    if (path[0] == '/') base = &root_;
    List<String> parts;
    split_path(path, parts);
    FSNode* cur = base;
    for (int i = 0; i < parts.size(); i++) {
        const String& seg = parts[i];
        if (seg == ".") continue;
        if (seg == "..") {
            if (cur->parent) cur = cur->parent;
            continue;
        }
        if (!cur->is_dir) return 0;
        FSNode* child = find_child(cur, seg.c_str());
        if (!child) return 0;
        cur = child;
    }
    return cur;
}

FSNode* VFS::resolve(const char* path) {
    return resolve_from(cwd, path);
}

FSNode* VFS::mkdir(const char* path) {
    if (!path || !*path) return 0;
    // 找到最后一个 '/'，父路径 + 新名
    String p = path;
    int last = p.rfind('/');
    String parent_path, name;
    if (last >= 0) {
        parent_path = p.substr(0, last);
        name = p.substr(last + 1, p.len() - last - 1);
        if (parent_path.empty()) parent_path = "/";
    } else {
        parent_path = ".";
        name = p;
    }
    if (name.empty()) return 0;
    FSNode* parent = resolve(parent_path.c_str());
    if (!parent || !parent->is_dir) return 0;
    if (find_child(parent, name.c_str())) return 0;
    FSNode* n = alloc_node();
    if (!n) return 0;
    n->name = name.c_str();
    n->is_dir = true;
    n->parent = parent;
    parent->children.push(n);
    return n;
}

FSNode* VFS::create_file(const char* path) {
    if (!path || !*path) return 0;
    String p = path;
    int last = p.rfind('/');
    String parent_path, name;
    if (last >= 0) {
        parent_path = p.substr(0, last);
        name = p.substr(last + 1, p.len() - last - 1);
        if (parent_path.empty()) parent_path = "/";
    } else {
        parent_path = ".";
        name = p;
    }
    if (name.empty()) return 0;
    FSNode* parent = resolve(parent_path.c_str());
    if (!parent || !parent->is_dir) return 0;
    FSNode* exist = find_child(parent, name.c_str());
    if (exist) {
        if (!exist->is_dir) return exist;  // 覆盖写
        return 0;
    }
    FSNode* n = alloc_node();
    if (!n) return 0;
    n->name = name.c_str();
    n->is_dir = false;
    n->parent = parent;
    parent->children.push(n);
    return n;
}

bool VFS::write_file(FSNode* f, const uint8_t* data, uint32_t size) {
    if (!f || f->is_dir) return false;
    uint8_t* buf = 0;
    if (size > 0) {
        buf = (uint8_t*)kalloc(size);
        if (!buf) return false;
        memcpy(buf, data, size);
    }
    if (f->data) kfree(f->data);
    f->data = buf;
    f->size = size;
    f->mtime = platform_tick_ms() / 1000;
    return true;
}

bool VFS::remove_node(FSNode* n) {
    if (!n || n == &root_ || !n->parent) return false;
    for (int i = 0; i < n->parent->children.size(); i++) {
        if (n->parent->children[i] == n) {
            n->parent->children.remove(i);
            free_tree(n);
            return true;
        }
    }
    return false;
}

// ---------- 垃圾桶（Trash） ----------
FSNode* VFS::trash_dir() {
    FSNode* d = resolve("/home/user/Trash");
    if (!d) {
        mkdir("/home/user");
        mkdir("/home/user/Trash");
        d = resolve("/home/user/Trash");
    }
    return d;
}

FSNode* VFS::trash_file(FSNode* n) {
    if (!n || n->is_dir) return 0;
    FSNode* td = trash_dir();
    if (!td) return 0;
    String name = n->name;
    int k = 1;
    while (find_child(td, name.c_str())) {
        char suf[16];
        ksprintf(suf, sizeof(suf), "_%d", k++);
        String base = n->name;
        int dot = base.rfind('.');
        if (dot >= 0) name = base.substr(0, dot) + suf + base.substr(dot, base.len() - dot);
        else name = base + suf;
    }
    return move_node(n, td, name.c_str());
}

FSNode* VFS::restore_file(FSNode* n, FSNode* dst_dir) {
    if (!n || !dst_dir || !dst_dir->is_dir) return 0;
    return move_node(n, dst_dir);
}

int VFS::empty_trash() {
    FSNode* td = trash_dir();
    if (!td) return 0;
    int c = 0;
    for (int i = td->children.size() - 1; i >= 0; i--) {
        if (!td->children[i]->is_dir) {
            remove_node(td->children[i]);
            c++;
        }
    }
    return c;
}

FSNode* VFS::move_node(FSNode* n, FSNode* dst_dir, const char* new_name) {
    if (!n || !dst_dir || !dst_dir->is_dir) return 0;
    if (n == &root_ || !n->parent || n == dst_dir) return 0;
    // 防止移入自身子树
    FSNode* p = dst_dir;
    while (p) { if (p == n) return 0; p = p->parent; }
    // 目标重名则失败（调用方可选自动改名）
    const char* nn = new_name ? new_name : n->name.c_str();
    if (find_child(dst_dir, nn)) return 0;
    // 从旧 parent 摘下
    for (int i = 0; i < n->parent->children.size(); i++) {
        if (n->parent->children[i] == n) { n->parent->children.remove(i); break; }
    }
    n->parent = dst_dir;
    n->name = nn;
    dst_dir->children.push(n);
    return n;
}

void VFS::set_cwd(FSNode* n) { if (n && n->is_dir) cwd = n; }

uint32_t VFS::total_bytes() {
    uint32_t sum = 0;
    // 简单递归
    struct Walk { static void go(FSNode* n, uint32_t& s) {
        if (!n->is_dir) s += n->size;
        for (int i = 0; i < n->children.size(); i++) go(n->children[i], s);
    } };
    Walk::go(&root_, sum);
    return sum;
}

int VFS::node_count() {
    int c = 0;
    struct Walk { static void go(FSNode* n, int& c) {
        c++;
        for (int i = 0; i < n->children.size(); i++) go(n->children[i], c);
    } };
    Walk::go(&root_, c);
    return c;
}

// ---------------- 默认文件树 ----------------
static void put_text(VFS* v, const char* path, const char* text) {
    FSNode* f = v->create_file(path);
    if (f) v->write_file(f, (const uint8_t*)text, (uint32_t)strlen(text));
}

static void put_bytes(VFS* v, const char* path, const uint8_t* bytes, uint32_t n) {
    FSNode* f = v->create_file(path);
    if (f) v->write_file(f, bytes, n);
}

// Remove legacy nodes created by an older browser_save_page that flattened
// the full path into one name (e.g. "_usr_downloads_page_search_file.html").
// They were created in the cwd, so scan every directory one level deep.
void VFS::cleanup_stray_nodes() {
    for (int i = root_.children.size() - 1; i >= 0; i--) {
        FSNode* c = root_.children[i];
        if (strncmp(c->name.c_str(), "_usr_downloads_page_", 20) == 0) {
            remove_node(c);
        } else if (c->is_dir) {
            for (int j = c->children.size() - 1; j >= 0; j--) {
                FSNode* g = c->children[j];
                if (strncmp(g->name.c_str(), "_usr_downloads_page_", 20) == 0)
                    remove_node(g);
            }
        }
    }
}

void VFS::create_default_tree() {
    mkdir("/bin");
    mkdir("/boot");
    mkdir("/dev");
    mkdir("/etc");
    mkdir("/home");
    mkdir("/home/user");
    mkdir("/home/user/Documents");
    mkdir("/home/user/Pictures");
    mkdir("/home/user/Music");
    mkdir("/home/user/Downloads");
    mkdir("/home/user/.Trash");
    mkdir("/lib");
    mkdir("/mnt");
    mkdir("/opt");
    mkdir("/proc");
    mkdir("/root");
    mkdir("/run");
    mkdir("/sbin");
    mkdir("/srv");
    mkdir("/sys");
    mkdir("/tmp");
    mkdir("/usr");
    mkdir("/usr/bin");
    mkdir("/usr/lib");
    mkdir("/usr/share");
    mkdir("/usr/share/apps");
    mkdir("/usr/downloads");
    mkdir("/var");
    mkdir("/var/log");
    mkdir("/var/cache");

    put_text(this, "/etc/nefu.conf",
        "# nefuOS configuration\n"
        "hostname=nefuos\n"
        "resolution=800x600\n"
        "theme=default\n"
        "user=guest\n"
        "version=0.2.0\n");
    put_text(this, "/etc/hostname", "nefuos\n");
    put_text(this, "/etc/os-release",
        "NAME=nefuOS\n"
        "VERSION=0.2.0\n"
        "ID=nefuos\n"
        "PRETTY_NAME=nefuOS 0.2.0\n");
    put_text(this, "/README.txt",
        "nefuOS v0.2 - 一个微型操作系统\n"
        "======================================\n"
        "Built with C++/C, dual backend:\n"
        "  - host: nefuOS.exe (Windows)\n"
        "  - bare: nefuOS.iso (El Torito no-emulation, QEMU)\n"
        "\n"
        "File system layout (like a real Unix):\n"
        "  /bin /boot /dev /etc /home /lib /mnt /opt\n"
        "  /proc /root /run /sbin /srv /sys /tmp /usr /var\n"
        "\n"
        "Try the Terminal: ls, cd, cat, mkdir, echo, ping.\n");
    put_text(this, "/home/user/Documents/hello.txt",
        "你好，欢迎使用 nefuOS！\n"
        "Hello from nefuOS!\n"
        "\n"
        "这个文件位于虚拟文件系统中。\n"
        "你可以在终端用 'echo text > file' 编辑它。\n");
    put_text(this, "/home/user/Documents/todo.txt",
        "TODO\n"
        "  1. boot nefuOS in QEMU\n"
        "  2. open File Manager\n"
        "  3. explore the file tree\n"
        "  4. install apps from the Store\n"
        "  5. have fun\n");
    put_text(this, "/home/user/Documents/nefuos.txt",
        "nefuOS\n"
        "-----\n"
        "一个拥有虚拟文件树的最小桌面操作系统，\n"
        "A minimal desktop OS with a virtual file tree,\n"
        "written in C++ and C, packaged as a bootable ISO.\n"
        "\n"
        "Architecture: shared core + dual backends.\n"
        "  core     : GUI, VFS, apps (platform independent)\n"
        "  win32    : host backend (nefuOS.exe)\n"
        "  bare     : x86_64 kernel backend (ISO, QEMU)\n"
        "  net      : e1000 + ARP/ICMP/TCP stack\n");
    put_text(this, "/home/user/Downloads/readme.txt",
        "下载文件夹 Downloads folder\n"
        "----------------\n"
        "系统下载的文件保存在这里。\n"
        "Files downloaded by the system land here.\n"
        "Use 'echo data > /usr/downloads/name.txt'\n"
        "or 'cp <src> /usr/downloads/' to store files.\n");
    put_text(this, "/usr/share/banner.txt",
        "    _   __ ____  _____  __  ____\n"
        "   / | / // __ \\/ ___/ / / / /  /\n"
        "  /  |/ / / / /\\__ \\ / /_/ / / / \n"
        " / /|  / /_/ /___/ // __  / /_/  \n"
        "/_/ |_/_____//____//_/ /_/ (_)   \n"
        "     a tiny operating system\n");
    put_text(this, "/usr/share/apps/calculator.nefud",
        "NEFUD1\n"
        "name=Calculator\n"
        "desc=Arithmetic calculator\n"
        "author=nefuOS\n"
        "ver=1.0\n");
    put_text(this, "/usr/share/apps/notepad.nefud",
        "NEFUD1\n"
        "name=Notepad\n"
        "desc=Simple text editor\n"
        "author=nefuOS\n"
        "ver=1.0\n");
    put_text(this, "/usr/share/apps/browser.nefud",
        "NEFUD1\n"
        "name=Browser\n"
        "desc=Web browser (TCP/HTTP)\n"
        "author=nefuOS\n"
        "ver=1.0\n");
    put_text(this, "/usr/share/apps/monitor.nefud",
        "NEFUD1\n"
        "name=System Monitor\n"
        "desc=CPU/memory/network monitor\n"
        "author=nefuOS\n"
        "ver=1.0\n");
    {
        // sample .bin application: NEFBIN01 package with NEFVM bytecode
        static const uint8_t hello_bin[] = {
            'N','E','F','B','I','N','0','1',
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            'N','E','F','V','M',' ','d','e','m','o',':',' ','h','e','l','l','o',' ','+',' ','m','a','t','h',0,0,0,0,0,0,0,0,0,
            0x01,'H',0x09,0x01,'e',0x09,0x01,'l',0x09,0x01,'l',0x09,0x01,'o',0x09,
            0x01,',',0x09,0x01,' ',0x09,0x01,'N',0x09,0x01,'E',0x09,0x01,'F',0x09,
            0x01,'U',0x09,0x01,'O',0x09,0x01,'S',0x09,0x01,'!',0x09,
            0x01,10,0x09,
            0x01,6,0x01,7,0x06,0x08,0x0A,
            0x01,100,0x01,8,0x05,0x08,0x0A,
            0x00
        };
        put_bytes(this, "/usr/share/apps/hello.bin", hello_bin, sizeof(hello_bin));
    }

    put_text(this, "/var/log/boot.log",
        "[ok] VFS mounted\n"
        "[ok] GUI initialized\n"
        "[ok] network link up\n"
        "[ok] system ready\n");
}

// ---------------- 序列化 ----------------
static const uint32_t SAVE_MAGIC = 0x4E465301; // "NFS1"

uint32_t VFS::ser_size(FSNode* n) {
    uint32_t s = 1 + 2 + (uint32_t)n->name.len() + 4;
    if (!n->is_dir) s += 4 + n->size;
    else s += 2 + (uint32_t)n->children.size() * 4;
    for (int i = 0; i < n->children.size(); i++) s += ser_size(n->children[i]);
    return s;
}

void VFS::ser_node(uint8_t*& p, FSNode* n) {
    *p++ = n->is_dir ? 0 : 1;
    uint16_t nl = (uint16_t)n->name.len();
    memcpy(p, &nl, 2); p += 2;
    memcpy(p, n->name.c_str(), nl); p += nl;
    if (n->is_dir) {
        uint16_t cc = (uint16_t)n->children.size();
        memcpy(p, &cc, 2); p += 2;
        for (int i = 0; i < n->children.size(); i++) ser_node(p, n->children[i]);
    } else {
        memcpy(p, &n->size, 4); p += 4;
        if (n->size) { memcpy(p, n->data, n->size); p += n->size; }
    }
}

bool VFS::deser_node(const uint8_t*& p, const uint8_t* end, FSNode* parent) {
    if (p >= end) return false;
    uint8_t type = *p++;
    uint16_t nl;
    if (p + 2 > end) return false;
    memcpy(&nl, p, 2); p += 2;
    if (p + nl > end) return false;
    String nm;
    for (int i = 0; i < nl; i++) nm += (char)p[i];
    p += nl;
    FSNode* n = alloc_node();
    if (!n) return false;
    n->name = nm.c_str();
    n->is_dir = (type == 0);
    n->parent = parent;
    if (n->is_dir) {
        uint16_t cc;
        if (p + 2 > end) { delete n; return false; }
        memcpy(&cc, p, 2); p += 2;
        for (int i = 0; i < cc; i++) {
            if (!deser_node(p, end, n)) { delete n; return false; }
        }
    } else {
        if (p + 4 > end) { delete n; return false; }
        memcpy(&n->size, p, 4); p += 4;
        if (n->size) {
            if (p + n->size > end) { delete n; return false; }
            n->data = (uint8_t*)kalloc(n->size);
            if (!n->data) { delete n; return false; }
            memcpy(n->data, p, n->size);
            p += n->size;
        }
    }
    parent->children.push(n);
    return true;
}

bool VFS::save(uint8_t** out, uint32_t* out_size) {
    uint32_t total = 4 + ser_size(&root_);
    uint8_t* buf = (uint8_t*)kalloc(total);
    if (!buf) return false;
    uint8_t* p = buf;
    memcpy(p, &SAVE_MAGIC, 4); p += 4;
    ser_node(p, &root_);
    *out = buf;
    *out_size = total;
    return true;
}

bool VFS::load(const uint8_t* data, uint32_t size) {
    if (!data || size < 4) return false;
    uint32_t magic;
    memcpy(&magic, data, 4);
    if (magic != SAVE_MAGIC) return false;
    const uint8_t* p = data + 4;
    const uint8_t* end = data + size;
    free_tree(&root_);
    root_.children.erase_all();
    root_.name = "/";
    root_.is_dir = true;
    root_.expanded = true;
    return deser_node(p, end, &root_);
}

} // namespace nefu
