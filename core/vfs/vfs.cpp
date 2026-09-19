// nefuOS
#include "vfs.h"
#include "../platform.h"
#include "../sys/admin_hash.h"   // NEFU_ADMIN_HASH (build-time injected hash only)

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
    // '/'， +
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
        if (!exist->is_dir) return exist;
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

// ---------- （Trash） ----------
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

    FSNode* p = dst_dir;
    while (p) { if (p == n) return 0; p = p->parent; }
    // （）
    const char* nn = new_name ? new_name : n->name.c_str();
    if (find_child(dst_dir, nn)) return 0;
    // parent
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

// ---------------- ----------------
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
// Returns true when a node name looks like a legacy flattened-path artifact
// left by an older browser_save_page (e.g. "_usr_downloads_page_foo_html").
static bool is_stray_name(const String& name) {
    if (name.len() == 0) return true;
    // illegal filenames: path separators and dot entries
    if (name == "/" || name == "\\" || name == "." || name == "..") return true;
    // any name containing a path separator is invalid as a single node
    for (int i = 0; i < name.len(); i++) {
        if (name[i] == '/' || name[i] == '\\') return true;
    }
    // non-printable or high-bit bytes are never valid in our VFS names
    for (int i = 0; i < name.len(); i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 32 || c > 126) return true;
    }
    // flattened-path patterns: starts with "_<dir>_" and is long
    static const char* prefixes[] = {
        "_usr_", "_home_", "_etc_", "_tmp_", "_bin_", "_var_", "_root_", "_mnt_", 0
    };
    for (int i = 0; prefixes[i]; i++) {
        int pl = (int)strlen(prefixes[i]);
        if (name.len() > pl + 8 && strncmp(name.c_str(), prefixes[i], pl) == 0) return true;
    }
    return false;
}

// Recursively remove legacy broken nodes from the whole tree.
static void cleanup_recursive(VFS* vfs, FSNode* n) {
    if (!n || !n->is_dir) return;
    for (int i = n->children.size() - 1; i >= 0; i--) {
        FSNode* c = n->children[i];
        if (is_stray_name(c->name)) {
            vfs->remove_node(c);
        } else {
            cleanup_recursive(vfs, c);
        }
    }
}

void VFS::cleanup_stray_nodes() {
    cleanup_recursive(this, &root_);
}

// Make sure the standard Unix-like hierarchy exists. Idempotent: existing
// dirs are left untouched. Called after loading a persisted snapshot so an
// old save can never hide the /usr /tmp ... structure.
void VFS::ensure_standard_dirs() {
    static const char* dirs[] = {
        "/bin", "/boot", "/dev", "/etc",
        "/home", "/home/user",
        "/home/user/Documents", "/home/user/Pictures", "/home/user/Music",
        "/home/user/Downloads", "/home/user/.Trash",
        "/lib", "/mnt", "/opt", "/proc", "/root", "/run", "/sbin", "/srv",
        "/sys", "/tmp", "/usr", "/usr/bin", "/usr/lib", "/usr/share",
        "/usr/share/apps", "/usr/share/man", "/usr/downloads", "/usr/local",
        "/usr/local/bin", "/usr/local/share", "/usr/local/share/man",
        "/usr/sbin", "/usr/src", "/usr/include", "/usr/games",
        "/etc/init.d",
        "/var", "/var/log", "/var/cache", "/var/lib", "/var/lib/dpkg", "/var/run",
        "/var/lib/nefuos", "/var/lib/nefuos/db",
        "/lib/modules", "/lib/firmware",
        "/mnt/cdrom", "/mnt/usb", "/mnt/hd",
        "/srv/www",
        "/root"
    };
    for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) mkdir(dirs[i]);
}

// Default user/system files, created only when missing so a persisted
// snapshot is never clobbered. Idempotent - run after ensure_standard_dirs
// so a snapshot loaded from storage still shows the standard contents.
void VFS::ensure_default_files() {
    struct { const char* path; const char* text; } files[] = {
        { "/etc/passwd",
          "root:x:0:0:root:/root:/bin/sh\n"
          "guest:x:1000:1000:guest:/home/user:/bin/sh\n" },
        { "/etc/group",
          "root:x:0:\nusers:x:100:\nguest:x:1000:\n" },
        { "/etc/hostname", "nefuos\n" },
        { "/etc/os-release",
          "NAME=nefuOS\nVERSION=0.2.0\nID=nefuos\nPRETTY_NAME=nefuOS 0.2.0\n" },
        { "/etc/nefu.conf",
          "# nefuOS configuration\nhostname=nefuos\nuser=guest\nversion=0.2.0\n" },
        { "/var/log/boot.log",
          "nefuOS boot log\n[ok] vfs mounted\n[ok] settings loaded\n"
          "[ok] disk scan done\n[ok] network up\n" },
        { "/var/log/syslog",
          "Sep 6 00:00:00 nefuos kernel: nefuOS 0.2.0 booting\n" },
        { "/var/log/honeypot.log",
          "# passive honeypot: records decoy-service connection attempts\n"
          "# (no decoy ports open by default)\n" },
        { "/home/user/Documents/hello.txt",
          "Hello from nefuOS!\nWelcome to your virtual file system.\n" },
        { "/home/user/Documents/todo.txt",
          "TODO\n 1. explore the file tree\n 2. try the terminal\n 3. install apps\n" },
        { "/usr/share/banner.txt", "nefuOS 0.2 - a tiny operating system\n" },
        { "/usr/share/apps/README",
          "Installed applications (launch from the desktop or Store):\n"
          "  filemgr  - two-pane file manager\n"
          "  browser  - web browser (file:// and http://)\n"
          "  terminal - shell with ~40 commands\n"
          "  settings - system settings and power\n"
          "  store    - software store (.nefud packages)\n"
          "  notepad  - text editor\n"
          "  imageviewer, jpeg, music, monitor, calc, clock,\n"
          "  minesweep, snake, paint, fontview, wiki, sysinfo\n" },
        { "/usr/share/man/nefuos.1",
          "NEFUOS(1)                 nefuOS Manual                NEFUOS(1)\n"
          "\n"
          "NAME\n"
          "       nefuos - a tiny dual-backend hobby operating system\n"
          "\n"
          "SYNOPSIS\n"
          "       nefuOS.exe  (Windows host)\n"
          "       nefuOS.iso  (bootable image for QEMU / VMs)\n"
          "\n"
          "FILESYSTEM\n"
          "       /usr  system programs and libraries\n"
          "       /tmp  temporary files (cleared on boot)\n"
          "       /home/user  user files\n"
          "\n"
          "SEE ALSO\n"
          "       terminal(1), filemgr(1), browser(1)\n" },
        { "/usr/local/share/nefuos-info.txt",
          "nefuOS - local additions\n"
          "This directory tree follows the FHS (Filesystem Hierarchy Standard)\n"
          "so software installed under /usr/local never mixes with system files.\n" },
        { "/usr/include/stdio.h",
          "/* nefuOS stdio.h - standard I/O interface (minimal) */\n"
          "#ifndef _NEFU_STDIO_H\n"
          "#define _NEFU_STDIO_H\n"
          "int printf(const char* fmt, ...);\n"
          "int puts(const char* s);\n"
          "#endif\n" },
        { "/usr/src/hello.c",
          "/* nefuOS sample C program */\n"
          "#include <stdio.h>\n"
          "int main(void) {\n"
          "    printf(\"Hello from nefuOS!\\n\");\n"
          "    return 0;\n"
          "}\n" },
        { "/opt/nefu/README",
          "nefuOS optional software directory\n"
          "Third-party packages are installed here.\n" },
        { "/var/log/lastlog",
          "nefuOS last login log (empty on fresh boot)\n" },
        { "/README.txt",
          "nefuOS v0.2 - dual-backend hobby OS (host exe + bootable ISO)\n" },
    };
    for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        if (!resolve(files[i].path)) put_text(this, files[i].path, files[i].text);
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
    mkdir("/usr/share/man");
    mkdir("/usr/downloads");
    mkdir("/usr/local");
    mkdir("/usr/local/bin");
    mkdir("/usr/local/share");
    mkdir("/usr/local/share/man");
    mkdir("/usr/sbin");
    mkdir("/usr/src");
    mkdir("/usr/include");
    mkdir("/usr/games");
    mkdir("/opt/nefu");
    mkdir("/var/tmp");
    mkdir("/var/spool");
    mkdir("/var/run");
    mkdir("/root/.config");
    mkdir("/home/user/.config");
    mkdir("/home/user/.local");
    mkdir("/home/user/.local/share");
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
        "   / | / // __ \\/ ___/ / / / / /\n"
        "  /  |/ / / / /\\__ \\ / /_/ / / / \n"
        " / /|  / /_/ /___/ // __ / /_/ \n"
        "/_/ |_/_____// ____//_/ /_/ (_) \n"
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
    put_text(this, "/usr/share/demo_form.html",
        "<html><body>\n"
        "<h2>nefuOS form demo (lithtml engine)</h2>\n"
        "<p>This page tests form support in the built-in browser.</p>\n"
        "<form action=\"http://10.0.2.2:8000/get\" method=\"get\">\n"
        "  <b>GET form:</b><br/>\n"
        "  <input name=\"q\" placeholder=\"search term\" />\n"
        "  <button name=\"go\" value=\"Search\">Search (GET)</button>\n"
        "</form>\n"
        "<form action=\"http://10.0.2.2:8000/post\" method=\"post\">\n"
        "  <b>POST form:</b><br/>\n"
        "  <input name=\"user\" placeholder=\"username\" />\n"
        "  <button name=\"send\" value=\"Submit\" method=\"post\">Send (POST)</button>\n"
        "</form>\n"
        "<hr/>\n"
        "<ul><li>cookie jar: /var/lib/nefuos/cookies.txt</li>\n"
        "<li>history: /home/user/.nefu_history</li></ul>\n"
        "</body></html>\n");
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
    // ---- /etc: real Unix config files ----
    put_text(this, "/etc/passwd", "root:x:0:0:root:/root:/bin/sh\nnefu:x:1000:1000:nefu:/home/user:/bin/sh\nlbinm:x:0:0:admin:/root:/bin/sh\nnobody:x:65534:65534:nobody:/nonexistent:/bin/false\n");
    put_text(this, "/etc/shadow",
        "root:*:0:0:99999:7:::\n"
        "lbinm:" NEFU_ADMIN_HASH ":0:0:99999:7:::\n"
        "nefu:!:0:0:99999:7:::\n");
    put_text(this, "/etc/group", "root:x:0:\nnefu:x:1000:\nwheel:x:10:nefu\naudio:x:29:\nvideo:x:44:\n");
    put_text(this, "/etc/hosts", "127.0.0.1 localhost\n127.0.1.1 nefuos\n::1 localhost\n10.0.2.2 gateway\n10.0.2.15 nefuos\n");
    put_text(this, "/etc/resolv.conf", "nameserver 8.8.8.8\nnameserver 8.8.4.4\nnameserver 1.1.1.1\n");
    put_text(this, "/etc/fstab", "proc /proc proc defaults 0 0\nsysfs /sys sysfs defaults 0 0\n/dev/vda1 / ext4 defaults 0 1\ntmpfs /tmp tmpfs defaults 0 0\n");
    put_text(this, "/etc/profile", "export PATH=/bin:/usr/bin:/sbin:/usr/sbin\nexport PS1='\\u@\\h:\\w\\$ '\nexport EDITOR=/bin/vi\numask 022\n");
    put_text(this, "/etc/shells", "/bin/sh\n/bin/bash\n/usr/bin/tmux\n");
    put_text(this, "/etc/motd", "Welcome to nefuOS!\n * Docs: https://github.com/Nefu-Dev/nefuOS\n");
    put_text(this, "/etc/services", "echo 7/tcp\nftp 21/tcp\nssh 22/tcp\ntelnet 23/tcp\nsmtp 25/tcp\ndomain 53/tcp\nhttp 80/tcp\nhttps 443/tcp\n");
    put_text(this, "/etc/inittab", "id:3:initdefault:\nsi::sysinit:/etc/init.d/rcS\n1:2345:respawn:/sbin/getty 38400 tty1\n");
    // ---- /proc: virtual filesystem ----
    put_text(this, "/proc/cpuinfo", "processor : 0\nvendor_id : GenuineIntel\nmodel name : nefuOS Virtual CPU (x86_64)\ncpu MHz : 2400.000\ncache size : 8192 KB\nbogomips : 4800.00\n");
    put_text(this, "/proc/meminfo", "MemTotal: 65536 kB\nMemFree: 61440 kB\nMemAvailable: 62000 kB\nBuffers: 512 kB\nCached: 2048 kB\nSwapTotal: 0 kB\n");
    put_text(this, "/proc/version", "nefuOS version 0.2.0 (root@nefuos) (gcc 13.2.0) #1 SMP x86_64\n");
    put_text(this, "/proc/uptime", "1234.56 1200.00\n");
    put_text(this, "/proc/loadavg", "0.05 0.10 0.15 1/64 128\n");
    put_text(this, "/proc/modules", "e1000 163840 0 - Live 0xffffffff00000000\nvfs 81920 0 - Live 0xffffffff00020000\ngui 65536 0 - Live 0xffffffff00040000\nnefvm 32768 0 - Live 0xffffffff00060000\n");
    put_text(this, "/proc/mounts", "rootfs / rootfs rw 0 0\nproc /proc proc rw 0 0\nsysfs /sys sysfs rw 0 0\ndevtmpfs /dev devtmpfs rw 0 0\n/dev/vda1 / ext4 rw 0 0\ntmpfs /tmp tmpfs rw 0 0\n");
    put_text(this, "/proc/filesystems", "nodev sysfs\nnodev rootfs\nnodev tmpfs\nnodev devtmpfs\n ext4\n vfat\n iso9660\n");
    put_text(this, "/proc/devices", "Character devices:\n 1 mem\n 4 tty\n 5 /dev/tty\n 10 misc\n180 usb\nBlock devices:\n 1 ramdisk\n 8 sd\n 11 sr\n");
    // ---- /dev: device nodes ----
    put_text(this, "/dev/null", "");
    put_text(this, "/dev/zero", "");
    put_text(this, "/dev/random", "");
    put_text(this, "/dev/tty", "");
    put_text(this, "/dev/console", "");
    put_text(this, "/dev/sda", "# 8GB virtual disk\n# /dev/sda1 ext4 /\n");
    put_text(this, "/dev/sr0", "# CD/DVD: nefuOS.iso\n");
    // ---- /sys: system info ----
    put_text(this, "/sys/kernel/hostname", "nefuos\n");
    put_text(this, "/sys/kernel/osrelease", "0.2.0-nefuOS\n");
    put_text(this, "/sys/class/net/eth0/address", "52:54:00:12:34:56\n");
    put_text(this, "/sys/class/net/eth0/operstate", "up\n");
    put_text(this, "/sys/class/net/eth0/speed", "1000\n");
    put_text(this, "/sys/class/net/eth0/mtu", "1500\n");
    // ---- /root: root user home ----
    put_text(this, "/root/.bashrc", "# root bashrc\nexport PS1='\\u@\\h:\\w# '\nalias ll='ls -l'\nalias la='ls -A'\nalias grep='grep --color=auto'\n");
    put_text(this, "/root/.profile", "# root profile\nif [ -f ~/.bashrc ]; then . ~/.bashrc; fi\n");
    put_text(this, "/root/.bash_history", "ls -la\ncd /etc\ncat passwd\nps aux\nifconfig\nping 10.0.2.2\ndf -h\nfree -m\nuname -a\n");
    // ---- /home/user: user config ----
    put_text(this, "/home/user/.bashrc", "# user bashrc\nexport PS1='\\u@\\h:\\w\\$ '\nalias ll='ls -l'\nalias ..='cd ..'\nalias grep='grep --color=auto'\nexport EDITOR=/bin/vi\n");
    put_text(this, "/home/user/.profile", "# user profile\nif [ -f ~/.bashrc ]; then . ~/.bashrc; fi\nexport PATH=$HOME/bin:$PATH\n");
    // ---- /bin: shell and coreutils stubs ----
    put_text(this, "/bin/sh", "# stub\n");
    put_text(this, "/bin/bash", "# stub\n");
    put_text(this, "/bin/ls", "# stub\n");
    put_text(this, "/bin/cat", "# stub\n");
    put_text(this, "/bin/mkdir", "# stub\n");
    put_text(this, "/bin/rm", "# stub\n");
    put_text(this, "/bin/cp", "# stub\n");
    put_text(this, "/bin/mv", "# stub\n");
    put_text(this, "/bin/grep", "# stub\n");
    put_text(this, "/bin/pwd", "# stub\n");
    put_text(this, "/bin/echo", "# stub\n");
    put_text(this, "/bin/true", "# stub\n");
    put_text(this, "/bin/false", "# stub\n");
    put_text(this, "/bin/hostname", "# stub\n");
    put_text(this, "/bin/uname", "# stub\n");
    put_text(this, "/bin/date", "# stub\n");
    put_text(this, "/bin/login", "# stub\n");
    put_text(this, "/bin/sleep", "# stub\n");
    // ---- /sbin: system binaries ----
    put_text(this, "/sbin/init", "# stub\n");
    put_text(this, "/sbin/reboot", "# stub\n");
    put_text(this, "/sbin/halt", "# stub\n");
    put_text(this, "/sbin/poweroff", "# stub\n");
    put_text(this, "/sbin/shutdown", "# stub\n");
    put_text(this, "/sbin/mount", "# stub\n");
    put_text(this, "/sbin/umount", "# stub\n");
    put_text(this, "/sbin/fsck", "# stub\n");
    put_text(this, "/sbin/mkfs", "# stub\n");
    put_text(this, "/sbin/modprobe", "# stub\n");
    put_text(this, "/sbin/lsmod", "# stub\n");
    put_text(this, "/sbin/ifconfig", "# stub\n");
    put_text(this, "/sbin/route", "# stub\n");
    // ---- /usr/bin: user binaries (compact stubs) ----
    put_text(this, "/usr/bin/top", "# stub\n");
    put_text(this, "/usr/bin/ps", "# stub\n");
    put_text(this, "/usr/bin/kill", "# stub\n");
    put_text(this, "/usr/bin/killall", "# stub\n");
    put_text(this, "/usr/bin/whoami", "# stub\n");
    put_text(this, "/usr/bin/id", "# stub\n");
    put_text(this, "/usr/bin/file", "# stub\n");
    put_text(this, "/usr/bin/stat", "# stub\n");
    put_text(this, "/usr/bin/ln", "# stub\n");
    put_text(this, "/usr/bin/wc", "# stub\n");
    put_text(this, "/usr/bin/head", "# stub\n");
    put_text(this, "/usr/bin/tail", "# stub\n");
    put_text(this, "/usr/bin/sort", "# stub\n");
    put_text(this, "/usr/bin/uniq", "# stub\n");
    put_text(this, "/usr/bin/diff", "# stub\n");
    put_text(this, "/usr/bin/sed", "# stub\n");
    put_text(this, "/usr/bin/awk", "# stub\n");
    put_text(this, "/usr/bin/cut", "# stub\n");
    put_text(this, "/usr/bin/tr", "# stub\n");
    put_text(this, "/usr/bin/tee", "# stub\n");
    put_text(this, "/usr/bin/find", "# stub\n");
    put_text(this, "/usr/bin/which", "# stub\n");
    put_text(this, "/usr/bin/env", "# stub\n");
    put_text(this, "/usr/bin/history", "# stub\n");
    put_text(this, "/usr/bin/clear", "# stub\n");
    put_text(this, "/usr/bin/man", "# stub\n");
    put_text(this, "/usr/bin/alias", "# stub\n");
    put_text(this, "/usr/bin/su", "# stub\n");
    put_text(this, "/usr/bin/sudo", "# stub\n");
    put_text(this, "/usr/bin/passwd", "# stub\n");
    put_text(this, "/usr/bin/chmod", "# stub\n");
    put_text(this, "/usr/bin/chown", "# stub\n");
    put_text(this, "/usr/bin/touch", "# stub\n");
    put_text(this, "/usr/bin/tree", "# stub\n");
    put_text(this, "/usr/bin/df", "# stub\n");
    put_text(this, "/usr/bin/du", "# stub\n");
    put_text(this, "/usr/bin/free", "# stub\n");
    put_text(this, "/usr/bin/uptime", "# stub\n");
    put_text(this, "/usr/bin/dmesg", "# stub\n");
    put_text(this, "/usr/bin/lspci", "# stub\n");
    put_text(this, "/usr/bin/lsusb", "# stub\n");
    put_text(this, "/usr/bin/ifconfig", "# stub\n");
    put_text(this, "/usr/bin/ping", "# stub\n");
    put_text(this, "/usr/bin/netstat", "# stub\n");
    put_text(this, "/usr/bin/wget", "# stub\n");
    put_text(this, "/usr/bin/curl", "# stub\n");
    put_text(this, "/usr/bin/ssh", "# stub\n");
    put_text(this, "/usr/bin/scp", "# stub\n");
    put_text(this, "/usr/bin/tar", "# stub\n");
    put_text(this, "/usr/bin/gzip", "# stub\n");
    put_text(this, "/usr/bin/vim", "# stub\n");
    put_text(this, "/usr/bin/gcc", "# stub\n");
    put_text(this, "/usr/bin/make", "# stub\n");
    put_text(this, "/usr/bin/git", "# stub\n");
    put_text(this, "/usr/bin/python3", "# stub\n");
    put_text(this, "/usr/bin/neofetch", "# stub\n");
    // ---- /usr/lib: libraries ----
    put_text(this, "/usr/lib/libc.so", "# GNU C Library stub\n# nefuOS uses klib\n");
    put_text(this, "/usr/lib/libm.so", "# math library stub\n");
    put_text(this, "/usr/lib/libpthread.so", "# POSIX threads stub\n");
    put_text(this, "/usr/lib/libdl.so", "# dynamic linking stub\n");
    // ---- /boot: boot files ----
    put_text(this, "/boot/vmlinuz", "# nefuOS kernel\n# loaded at 0x20000 by boot.s\n");
    put_text(this, "/boot/initrd.img", "# initial ramdisk\n");
    put_text(this, "/boot/grub/grub.cfg", "# nefuOS GRUB\nset default=0\nset timeout=3\nmenuentry nefuOS {\n  linux /boot/vmlinuz root=/dev/vda1 rw\n  initrd /boot/initrd.img\n}\n");
    put_text(this, "/boot/config-0.2.0", "# nefuOS kernel config\nCONFIG_X86_64=y\nCONFIG_VFS=y\nCONFIG_GUI=y\nCONFIG_NEFVM=y\nCONFIG_E1000=y\nCONFIG_TCP=y\n");
    // ---- /var/log: more log files ----
    put_text(this, "/var/log/syslog", "Sep 6 00:00:01 nefuos kernel: nefuOS 0.2.0 starting\nSep 6 00:00:01 nefuos kernel: VFS: mounted root\nSep 6 00:00:01 nefuos kernel: GUI: 800x600 framebuffer\nSep 6 00:00:01 nefuos kernel: e1000: NIC up\nSep 6 00:00:02 nefuos login: session opened for nefu\n");
    put_text(this, "/var/log/kern.log", "[0.000000] nefuOS 0.2.0 (gcc 13.2.0) #1 SMP\n[0.001234] VFS: mounted root filesystem\n[0.002345] GUI: framebuffer at 0xFD000000\n[0.003456] e1000: eth0 link up 1000Mbps\n[0.004567] TCP: stack initialized\n");
    put_text(this, "/var/log/auth.log", "Sep 6 00:00:02 nefuos login: session opened for user nefu\nSep 6 00:01:00 nefuos sudo: nefu : COMMAND=/bin/ls /root\n");
    put_text(this, "/var/log/dpkg.log", "2026-09-06 00:00:01 install base-files:all 0.2.0\n2026-09-06 00:00:01 install bash:amd64 5.2.15\n2026-09-06 00:00:01 install coreutils:amd64 9.4\n2026-09-06 00:00:01 install nefuos-desktop:amd64 0.2.0\n");
    // ---- /var/lib: system state ----
    put_text(this, "/var/lib/dpkg/status", "Package: base-files\nStatus: install ok installed\nVersion: 0.2.0\nDescription: nefuOS base system files\n\nPackage: nefuos-desktop\nStatus: install ok installed\nVersion: 0.2.0\nDescription: nefuOS desktop environment\n");
    // ---- /var/lib/nefuos: built-in key-value database ----
    put_text(this, "/var/lib/nefuos/db/system.db", "name=nefuOS\nversion=0.2.0\narch=x86_64\nboot=el-torito-no-emulation\nusers=root,nefu,lbinm\n");
    // ---- /tmp: temp files ----
    put_text(this, "/tmp/nefuos_tmp.txt", "# temporary file\n# cleared on reboot\n");
    put_text(this, "/tmp/.X0-lock", "1234\n");
    // ---- /opt: optional software ----
    put_text(this, "/opt/README", "# /opt: optional software\n");
    // ---- /srv: service data ----
    put_text(this, "/srv/README", "# /srv: service data\n");
    put_text(this, "/srv/www/index.html", "<!DOCTYPE html>\n<html><head><title>nefuOS</title></head><body><h1>Welcome to nefuOS!</h1></body></html>\n");
    // ---- /mnt: mount points ----
    put_text(this, "/mnt/README", "# /mnt: mount points\n");
    put_text(this, "/mnt/cdrom/README", "# CD-ROM mount point\n");
    put_text(this, "/mnt/usb/README", "# USB mount point\n");
    // ---- /lib: kernel modules ----
    put_text(this, "/lib/modules/0.2.0-nefuOS/modules.dep", "# modules.dep\nkernel/drivers/net/e1000.ko:\nkernel/fs/vfs/vfs.ko:\nkernel/gui/gui.ko:\nkernel/vm/nefvm.ko:\n");
    put_text(this, "/lib/modules/0.2.0-nefuOS/modules.alias", "# modules.alias\nalias pci:v00008086d0000100Esv*sd*bc*sc*i* e1000\n");
    put_text(this, "/lib/firmware/e1000.bin", "# e1000 firmware\n");
}

// ---------------- serialize ----------------
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
