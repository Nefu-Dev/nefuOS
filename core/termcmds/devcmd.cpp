// =============================================================================
//  devcmd.cpp —— 开发/工具命令实现
// =============================================================================
#include "devcmd.h"

namespace nefu {
namespace termcmds {

// -----------------------------------------------------------------------------
//  strings
// -----------------------------------------------------------------------------
static inline bool is_printable(unsigned char c) {
    return c >= 0x20 && c <= 0x7e;
}

void strings_extract(const char* data, int len, int min_len,
                     nefu::List<nefu::String>& out) {
    if (!data || len <= 0) return;
    int start = -1;
    for (int i = 0; i <= len; i++) {
        bool ok = (i < len) && is_printable((unsigned char)data[i]);
        if (ok && start < 0) start = i;
        if (!ok) {
            if (start >= 0 && i - start >= min_len) {
                out.push(nefu::String(data + start, i - start));
            }
            start = -1;
        }
    }
}

// -----------------------------------------------------------------------------
//  file( magic 探测)
// -----------------------------------------------------------------------------
const char* file_detect(const char* data, int len) {
    if (!data || len < 4) return "data";
    const uint8_t* d = (const uint8_t*)data;
    if (d[0] == 0x7f && d[1] == 'E' && d[2] == 'L' && d[3] == 'F')
        return "ELF 64-bit LSB executable";
    if (d[0] == 'M' && d[1] == 'Z')
        return "PE32+ executable (Windows)";
    if (len >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G')
        return "PNG image data";
    if (d[0] == 0xff && d[1] == 0xd8 && d[2] == 0xff)
        return "JPEG image data";
    if (d[0] == 'P' && d[1] == 'K' && d[2] == 0x03 && d[3] == 0x04)
        return "Zip archive data";
    if (d[0] == '#' && d[1] == '!')
        return "script, ASCII text";
    // 判断是否纯文本(可打印 + 常见控制符)
    int printable = 0, total = len < 256 ? len : 256;
    for (int i = 0; i < total; i++) {
        unsigned char c = d[i];
        if (is_printable(c) || c == '\n' || c == '\r' || c == '\t') printable++;
    }
    if (printable * 10 >= total * 9) return "UTF-8 Unicode text";
    return "data";
}

// -----------------------------------------------------------------------------
//  find(递归 VFS)
// -----------------------------------------------------------------------------
void find_walk(VfsNode* dir, const char* prefix, const char* pattern,
               nefu::List<nefu::String>& out) {
    if (!dir) return;
    for (int i = 0; i < dir->children.size(); i++) {
        VfsNode* c = dir->children[i];
        nefu::String full = prefix;
        full += "/";
        full += c->name;
        if (glob_match(pattern, c->name)) out.push(full);
        if (c->is_dir) find_walk(c, full.c_str(), pattern, out);
    }
}

// -----------------------------------------------------------------------------
//  history 环形缓冲
// -----------------------------------------------------------------------------
static nefu::String g_hist[32];
static int g_hist_head = 0;  // 下一个写入位置
static int g_hist_count = 0;

void history_clear() {
    for (int i = 0; i < 32; i++) g_hist[i].clear();
    g_hist_head = 0; g_hist_count = 0;
}

void history_push(const char* cmd) {
    if (!cmd || !*cmd) return;
    g_hist[g_hist_head] = cmd;
    g_hist_head = (g_hist_head + 1) % 32;
    if (g_hist_count < 32) g_hist_count++;
}

int history_count() { return g_hist_count; }

const char* history_get(int i) {
    if (i < 0 || i >= g_hist_count) return "";
    // 最旧的在 (head - count + 32) % 32
    int idx = ((g_hist_head - g_hist_count) + i + 32) % 32;
    return g_hist[idx].c_str();
}

// -----------------------------------------------------------------------------
//  命令
// -----------------------------------------------------------------------------
static int cmd_xxd(int argc, const char** argv, TermOutput* out) {
    if (argc != 2) { out->pln("usage: xxd <file>"); return 1; }
    nefu::String data;
    if (!vfs_read_file(argv[1], data)) { out->pfln("xxd: %s not found", argv[1]); return 1; }
    const char* p = data.c_str();
    int n = data.len();
    char line[128];
    for (int off = 0; off < n; off += 16) {
        int chunk = n - off; if (chunk > 16) chunk = 16;
        term_snprintf(line, sizeof(line), "%08x: ", off);
        out->p(line);
        for (int i = 0; i < 16; i++) {
            if (i < chunk) term_snprintf(line, sizeof(line), "%02x ", (unsigned char)p[off + i]);
            else term_snprintf(line, sizeof(line), "   ");
            out->p(line);
            if (i == 7) out->p(" ");
        }
        out->p(" ");
        for (int i = 0; i < chunk; i++) {
            unsigned char c = (unsigned char)p[off + i];
            out->pch(is_printable(c) ? (char)c : '.');
        }
        out->pln();
    }
    return 0;
}

static int cmd_hexdump(int argc, const char** argv, TermOutput* out) {
    // canonical -C 风格(与 xxd 略有不同的排版)
    if (argc != 2) { out->pln("usage: hexdump -C <file>"); return 1; }
    nefu::String data;
    if (!vfs_read_file(argv[1], data)) { out->pfln("hexdump: %s not found", argv[1]); return 1; }
    const char* p = data.c_str();
    int n = data.len();
    char line[96];
    for (int off = 0; off < n; off += 16) {
        int chunk = n - off; if (chunk > 16) chunk = 16;
        term_snprintf(line, sizeof(line), "%08x  ", off);
        out->p(line);
        for (int i = 0; i < chunk; i++) {
            term_snprintf(line, sizeof(line), "%02x ", (unsigned char)p[off + i]);
            out->p(line);
        }
        for (int i = chunk; i < 16; i++) out->p("   ");
        out->p(" |");
        for (int i = 0; i < chunk; i++) {
            unsigned char c = (unsigned char)p[off + i];
            out->pch(is_printable(c) ? (char)c : '.');
        }
        out->pln("|");
    }
    return 0;
}

static int cmd_strings(int argc, const char** argv, TermOutput* out) {
    if (argc != 2) { out->pln("usage: strings <file>"); return 1; }
    nefu::String data;
    if (!vfs_read_file(argv[1], data)) { out->pfln("strings: %s not found", argv[1]); return 1; }
    nefu::List<nefu::String> res;
    strings_extract(data.c_str(), data.len(), 4, res);
    for (int i = 0; i < res.size(); i++) out->pln(res[i].c_str());
    return 0;
}

static int cmd_file(int argc, const char** argv, TermOutput* out) {
    if (argc != 2) { out->pln("usage: file <file>"); return 1; }
    nefu::String data;
    if (!vfs_read_file(argv[1], data)) { out->pfln("file: %s not found", argv[1]); return 1; }
    out->pfln("%s: %s", argv[1], file_detect(data.c_str(), data.len()));
    return 0;
}

static int cmd_which(int argc, const char** argv, TermOutput* out) {
    if (argc != 2) { out->pln("usage: which <cmd>"); return 1; }
    // PATH 模拟: /bin 与 /usr/bin
    const char* dirs[] = {"/bin", "/usr/bin"};
    bool found = false;
    for (int i = 0; i < 2; i++) {
        nefu::String full = dirs[i];
        full += "/";
        full += argv[1];
        if (vfs_resolve(full.c_str())) { out->pln(full.c_str()); found = true; }
    }
    if (!found) out->pfln("which: no %s in PATH", argv[1]);
    return 1;
}

static int cmd_find(int argc, const char** argv, TermOutput* out) {
    // find [path] -name pattern
    const char* path = "/";
    const char* pattern = "*";
    for (int i = 1; i < argc; i++) {
        if (nefu::strcmp(argv[i], "-name") == 0 && i + 1 < argc) { pattern = argv[++i]; }
        else path = argv[i];
    }
    VfsNode* root = vfs_resolve(path);
    if (!root) { out->pfln("find: '%s': no such directory", path); return 1; }
    nefu::List<nefu::String> res;
    find_walk(root, nefu::String(path).c_str(), pattern, res);
    for (int i = 0; i < res.size(); i++) out->pln(res[i].c_str());
    return 0;
}

static int cmd_man(int argc, const char** argv, TermOutput* out) {
    if (argc != 2) { out->pln("usage: man <cmd>"); return 1; }
    struct Page { const char* name; const char* text; };
    static const Page pages[] = {
        {"ls",   "LS(1)  list directory contents\n  ls [-l] [-a] [dir]\n  List files. -l long format, -a show hidden."},
        {"grep", "GREP(1)  print lines matching a pattern\n  grep [-ivc] PATTERN [file]"},
        {"ping", "PING(8)  send ICMP ECHO_REQUEST\n  ping <host>"},
        {"cp",   "CP(1)  copy files\n  cp <src> <dst>"},
    };
    for (unsigned i = 0; i < sizeof(pages)/sizeof(pages[0]); i++) {
        if (nefu::strcmp(pages[i].name, argv[1]) == 0) {
            out->pln(pages[i].text);
            return 0;
        }
    }
    out->pfln("No manual entry for %s", argv[1]);
    return 1;
}

static int cmd_help(int argc, const char** argv, TermOutput* out) {
    (void)argc; (void)argv;
    out->pln("nefuOS builtin commands:");
    out->pln("  file:   touch rm mkdir rmdir cp mv ls tree du stat");
    out->pln("  text:   grep sed awk sort uniq wc head tail rev cat");
    out->pln("  sys:    ps uptime free hostname uname whoami date cal clear echo");
    out->pln("  net:    ping ifconfig route arp netstat nslookup curl");
    out->pln("  dev:    xxd hexdump strings file which find man help history");
    out->pln("  fun:    cowsay fortune sl figlet random joke quote");
    return 0;
}

static int cmd_history(int argc, const char** argv, TermOutput* out) {
    (void)argc; (void)argv;
    for (int i = 0; i < history_count(); i++) {
        out->pfln("%5d  %s", i + 1, history_get(i));
    }
    return 0;
}

// -----------------------------------------------------------------------------
//  self_test
// -----------------------------------------------------------------------------
int devcmd_self_test() {
    int fails = 0;
    vfs_init();
    while (vfs_root()->children.size()) vfs_remove(vfs_root()->children[0]->name);

    // 1) strings 提取(含嵌入 NUL, 手动给长度)
    {
        const char blob[] = "bin\0\0hello world\0\0\0short\0AB";
        int bloblen = (int)sizeof(blob) - 1; // 去掉编译期追加的结尾 NUL
        nefu::List<nefu::String> res;
        strings_extract(blob, bloblen, 4, res);
        bool found = false;
        for (int i = 0; i < res.size(); i++) if (res[i] == "hello world") found = true;
        if (!found) fails++;
    }
    // 2) file_detect magic
    {
        char elf[5] = {0x7f, 'E', 'L', 'F', 0};
        if (nefu::strstr(file_detect(elf, 4), "ELF") == 0) fails++;
        char png[8] = {(char)0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
        if (nefu::strstr(file_detect(png, 8), "PNG") == 0) fails++;
        if (nefu::strstr(file_detect("hello\nworld\n", 12), "text") == 0) fails++;
    }
    // 3) xxd 输出包含 offset 与 ASCII
    {
        vfs_mkdir("/bin", true);
        vfs_write_file("/bin/x", "ABCDEFGH", 8);
        const char* av[2] = {"xxd", "/bin/x"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_xxd(2, av, &o);
        if (!b.contains("00000000")) fails++;
        if (!b.contains("ABCDEFGH")) fails++;
    }
    // 4) which
    {
        vfs_mkdir("/bin", true);
        vfs_touch("/bin/ls");
        const char* av[2] = {"which", "ls"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_which(2, av, &o);
        if (!b.contains("/bin/ls")) fails++;
    }
    // 5) find
    {
        vfs_mkdir("/usr/include", true);
        vfs_write_file("/usr/include/a.h", "x", 1);
        vfs_write_file("/usr/include/b.h", "y", 1);
        vfs_write_file("/usr/include/c.c", "z", 1);
        const char* av[4] = {"find", "/usr", "-name", "*.h"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_find(4, av, &o);
        if (!b.contains("a.h")) fails++;
        if (!b.contains("b.h")) fails++;
        if (b.contains("c.c")) fails++;
    }
    // 6) history
    {
        history_clear();
        history_push("ls -l");
        history_push("grep foo bar");
        if (history_count() != 2) fails++;
        if (nefu::strcmp(history_get(0), "ls -l") != 0) fails++;
        if (nefu::strcmp(history_get(1), "grep foo bar") != 0) fails++;
    }
    // 7) man / help
    {
        const char* av[2] = {"man", "ls"};
        BufferTermOutput b; TermOutput o = b.out();
        cmd_man(2, av, &o);
        if (!b.contains("list directory")) fails++;
        b.clear();
        cmd_help(0, 0, &o);
        if (!b.contains("grep")) fails++;
    }
    vfs_shutdown();
    return fails;
}

} // namespace termcmds
} // namespace nefu
