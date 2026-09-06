// nefuOS 终端应用
#include "apps.h"
#include "nefvm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../net/net.h"

namespace nefu {

struct TermState {
    List<String> lines;
    String input;
    int cursor;
    int view_scroll;
    Window* win;
};

static const uint32_t TERM_BG = 0x000C0E12;
static const uint32_t TERM_FG = 0x00D6E2D8;
static const uint32_t TERM_PM = 0x007FBF7F;
static const uint32_t TERM_ERR = 0x00E06C75;

static int term_cols(TermState* t) { return t->win->content_w / 8; }
static int term_rows(TermState* t) { return t->win->content_h / 16; }

static void term_add(TermState* t, const char* line) {
    if (!line) return;
    int cols = term_cols(t);
    if (cols < 1) cols = 40;
    const char* p = line;
    while (*p) {
        int n = 0;
        while (p[n] && n < cols) n++;
        String s;
        for (int i = 0; i < n; i++) s += p[i];
        t->lines.push(s);
        p += n;
        while (t->lines.size() > 1200) t->lines.remove(0);
    }
    if (!*line) t->lines.push(String());
}

static void term_print(TermState* t, const char* s) { term_add(t, s); }

static String prompt_str() {
    // Unix-like: user@nefuos:/path$
    String p = "user@nefuos:";
    String cwd = node_path(g_vfs->cwd);
    if (cwd == "/home/user") p += "~";
    else p += cwd;
    p += "$ ";
    return p;
}

static void term_help(TermState* t) {
    term_print(t, "nefuOS shell v0.3 (Unix-like)");
    term_print(t, "  help              - this help");
    term_print(t, "  ls [-l] [path]    - list directory");
    term_print(t, "  cd <path>         - change directory");
    term_print(t, "  pwd               - print working dir");
    term_print(t, "  cat <file>...     - print files");
    term_print(t, "  mkdir [-p] <path> - create directory");
    term_print(t, "  touch <file>      - create empty file");
    term_print(t, "  rm <path>         - remove file/dir");
    term_print(t, "  cp <src> <dst>    - copy file");
    term_print(t, "  mv <src> <dst>    - move/rename file");
    term_print(t, "  echo <text>       - print text");
    term_print(t, "  echo <t> > <file> - write text to file");
    term_print(t, "  tree [path]       - show file tree");
    term_print(t, "  find [path]       - recursive listing");
    term_print(t, "  run <file.nefud>  - launch a .nefud app");
    term_print(t, "  whoami            - current user");
    term_print(t, "  uname             - system info");
    term_print(t, "  date              - uptime date");
    term_print(t, "  uptime            - time since boot");
    term_print(t, "  free              - memory status");
    term_print(t, "  ps                - process list");
    term_print(t, "  netstat           - network status");
    term_print(t, "  ping <ip>         - ICMP echo test");
    term_print(t, "  grep <pat> <file> - filter lines");
    term_print(t, "  wc <file>         - line/word/byte count");
    term_print(t, "  head/tail <file>  - first/last lines");
    term_print(t, "  sort <file>       - sort lines");
    term_print(t, "  uniq <file>       - unique lines");
    term_print(t, "  which <cmd>       - locate a command");
    term_print(t, "  env               - print environment");
    term_print(t, "  export K=V        - set environment var");
    term_print(t, "  history           - command history");
    term_print(t, "  df                - filesystem usage");
    term_print(t, "  du <path>         - directory size");
    term_print(t, "  chmod <mode> <f>  - set permissions (sim)");
    term_print(t, "  cat -n <file>     - numbered lines");
    term_print(t, "  trash <file>      - move to Trash");
    term_print(t, "  restore <file>    - restore from Trash");
    term_print(t, "  empty-trash       - empty Trash");
    term_print(t, "  clear             - clear screen");
    term_print(t, "  about             - about nefuOS");
    term_print(t, "  exit              - close terminal");
    term_print(t, "  shutdown          - power off");
}

static void term_ls(TermState* t, const char* path) {
    FSNode* d = path && *path ? g_vfs->resolve(path) : g_vfs->cwd;
    if (!d) { String e = "ls: no such path: "; e += path; term_print(t, e.c_str()); return; }
    if (!d->is_dir) { term_print(t, d->name.c_str()); return; }
    if (d->children.empty()) { term_print(t, "(empty)"); return; }
    int files = 0, dirs = 0;
    for (int i = 0; i < d->children.size(); i++) {
        FSNode* c = d->children[i];
        String line = c->name;
        if (c->is_dir) {
            line += "/";
            dirs++;
        } else {
            char sz[32];
            ksprintf(sz, sizeof(sz), "  (%u bytes)", c->size);
            line += sz;
            files++;
        }
        term_print(t, line.c_str());
    }
    char sum[64];
    ksprintf(sum, sizeof(sum), "%d files, %d dirs", files, dirs);
    term_print(t, sum);
}

static void term_cat(TermState* t, const char* path) {
    FSNode* f = g_vfs->resolve(path);
    if (!f) { String e = "cat: no such file: "; e += path; term_print(t, e.c_str()); return; }
    if (f->is_dir) { String e = "cat: is a directory: "; e += path; term_print(t, e.c_str()); return; }
    if (f->size == 0) return;
    char* buf = (char*)kalloc((size_t)f->size + 1);
    if (!buf) return;
    memcpy(buf, f->data, f->size);
    buf[f->size] = 0;
    // 逐行输出
    char* line = buf;
    for (uint32_t i = 0; i < f->size; i++) {
        if (buf[i] == '\n') { buf[i] = 0; term_print(t, line); line = buf + i + 1; }
    }
    if (*line) term_print(t, line);
    kfree(buf);
}

// ---------- shell history & environment ----------
#define HIST_MAX 16
static char s_history[HIST_MAX][128];
static int  s_hist_n = 0;
static void hist_add(const char* cmd) {
    if (!cmd || !*cmd) return;
    for (int i = 0; i < s_hist_n; i++) {
        if (strcmp(s_history[i], cmd) == 0) { return; }   // dedupe
    }
    if (s_hist_n < HIST_MAX) {
        strncpy(s_history[s_hist_n], cmd, 127);
        s_history[s_hist_n][127] = 0;
        s_hist_n++;
    } else {
        for (int i = 0; i < HIST_MAX - 1; i++) strcpy(s_history[i], s_history[i + 1]);
        strncpy(s_history[HIST_MAX - 1], cmd, 127);
        s_history[HIST_MAX - 1][127] = 0;
    }
}

static const char* s_env_names[8] = { "USER", "HOME", "SHELL", "PATH", "TERM", "HOSTNAME", "PWD", "OLDPWD" };
static char s_env_vals[8][96] = {
    "nefu", "/home/user", "/bin/sh", "/bin:/usr/bin:/usr/local/bin",
    "xterm-256color", "nefuos", "/", "/home/user"
};
static int env_index(const char* name) {
    for (int i = 0; i < 8; i++) if (strcmp(s_env_names[i], name) == 0) return i;
    return -1;
}

static void print_lines_from_file(TermState* t, FSNode* f, int start_line, int count, bool numbered) {
    if (!f || f->is_dir) { term_print(t, "not a file"); return; }
    List<String> ls;
    file_to_lines(f, ls, 4096);
    int n = ls.size();
    if (start_line >= n) return;
    int end = start_line + count;
    if (end > n) end = n;
    for (int i = start_line; i < end; i++) {
        if (numbered) {
            char buf[160];
            ksprintf(buf, sizeof(buf), "%4d  %s", i + 1, ls[i].c_str());
            term_print(t, buf);
        } else {
            term_print(t, ls[i].c_str());
        }
    }
}

// ---------- helpers for new commands ----------
static uint32_t parse_ip(const char* s) {
    if (!s) return 0;
    uint32_t ip = 0;
    for (int i = 0; i < 4; i++) {
        int v = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
        if (v > 255) v = 255;
        ip |= (uint32_t)v << (8 * (3 - i));
        if (*s == '.') s++;
    }
    return ip;
}

static void term_mkdir_p(TermState* t, const char* path) {
    if (!path || !*path) return;
    String acc = path[0] == '/' ? "/" : "";
    const char* p = path;
    bool first = true;
    while (*p) {
        while (*p == '/') p++;
        int start = (int)(p - path);
        while (*p && *p != '/') p++;
        if (p - path > start) {
            String seg;
            for (int k = start; k < (int)(p - path); k++) seg += path[k];
            if (first && acc == "/") { acc = acc + seg; first = false; }
            else { acc = acc + "/" + seg; }
            if (!g_vfs->resolve(acc.c_str())) {
                if (!g_vfs->mkdir(acc.c_str())) {
                    String e = "mkdir: cannot create: "; e += acc; term_print(t, e.c_str());
                    return;
                }
            }
        }
    }
    String ok = "created: "; ok += path; term_print(t, ok.c_str());
}

static bool fs_copy(FSNode* src, const char* dst_path) {
    FSNode* dst = g_vfs->create_file(dst_path);
    if (!dst) return false;
    if (src->size > 0) return g_vfs->write_file(dst, src->data, src->size);
    return true;
}

static void term_vm_out(const char* s, void* ud) { term_print((TermState*)ud, s); }

static void term_nefud_run(TermState* t, FSNode* f) {
    if (!f || f->is_dir || f->size == 0) return;
    // .bin: real binary package - launch bound app or execute NEFVM bytecode
    if (f->size >= 64 && f->data[0] == 'N' && f->data[1] == 'E' && f->data[2] == 'F' &&
        f->data[3] == 'B' && f->data[4] == 'I' && f->data[5] == 'N' &&
        f->data[6] == '0' && f->data[7] == '1') {
        char nm[25]; nm[0] = 0;
        for (int i = 0; i < 24 && f->data[8 + i]; i++) if (i < 24) nm[i] = (char)f->data[8 + i];
        nm[24] = 0;
        int app = nm[0] ? nefud_name_to_app_id(nm) : -1;
        if (app >= 0) {
            String ok = "launching: "; ok += nm; term_print(t, ok.c_str());
            app_launch(app);
        } else {
            term_print(t, "running NEFVM bytecode...\n");
            nefvm_run(f->data, f->size, term_vm_out, t);
            term_print(t, "\n[program exited]");
        }
        return;
    }
    // .nefud text manifest
    char* buf = (char*)kalloc((size_t)f->size + 1);
    if (!buf) return;
    memcpy(buf, f->data, f->size);
    buf[f->size] = 0;
    const char* nm = 0;
    char* line = buf;
    for (uint32_t i = 0; i < f->size; i++) {
        if (buf[i] == '\n') {
            buf[i] = 0;
            if (strncmp(line, "name=", 5) == 0) { nm = line + 5; break; }
            line = buf + i + 1;
        }
    }
    if (!nm && strncmp(line, "name=", 5) == 0) nm = line + 5;
    int app = nm ? nefud_name_to_app_id(nm) : -1;
    if (app >= 0) {
        String ok = "launching: "; ok += nm ? nm : "?"; term_print(t, ok.c_str());
        app_launch(app);
    } else {
        String e = "run: no app bound to: "; e += nm ? nm : "?"; term_print(t, e.c_str());
        app_show_textview(f);
    }
    kfree(buf);
}

static void term_run(TermState* t, const char* cmd) {
    if (!cmd || !*cmd) return;
    hist_add(cmd);
    // 拆分参数（就地修改输入缓冲）
    char* buf = t->input.data();
    const char* argv[8];
    int argc = 0;
    char* p = buf;
    while (*p && argc < 8) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) { *p++ = 0; }
    }
    const char* a0 = argc > 0 ? argv[0] : "";

    if (strcmp(a0, "help") == 0) term_help(t);
    else if (strcmp(a0, "ls") == 0) {
        if (argc > 1 && strcmp(argv[1], "-l") == 0) {
            FSNode* d = argc > 2 ? g_vfs->resolve(argv[2]) : g_vfs->cwd;
            if (!d) term_print(t, "ls: no such path");
            else {
                int files = 0, dirs = 0;
                for (int i = 0; i < d->children.size(); i++) {
                    FSNode* c = d->children[i];
                    char line[128];
                    if (c->is_dir) {
                        ksprintf(line, sizeof(line), "drwxr-xr-x  nefu nefu  %5u  %s/",
                                 (unsigned)c->children.size(), c->name.c_str());
                        dirs++;
                    } else {
                        ksprintf(line, sizeof(line), "-rw-r--r--  nefu nefu  %5u  %s",
                                 (unsigned)c->size, c->name.c_str());
                        files++;
                    }
                    term_print(t, line);
                }
                char sum[64];
                ksprintf(sum, sizeof(sum), "%d files, %d dirs", files, dirs);
                term_print(t, sum);
            }
        } else term_ls(t, argc > 1 ? argv[1] : 0);
    }
    else if (strcmp(a0, "cd") == 0) {
        if (argc < 2) { g_vfs->set_cwd(g_vfs->root()); }
        else {
            FSNode* d = g_vfs->resolve(argv[1]);
            if (!d || !d->is_dir) { String e = "cd: no such directory: "; e += argv[1]; term_print(t, e.c_str()); }
            else g_vfs->set_cwd(d);
        }
    }
    else if (strcmp(a0, "pwd") == 0) term_print(t, node_path(g_vfs->cwd).c_str());
    else if (strcmp(a0, "cat") == 0) { if (argc > 1) term_cat(t, argv[1]); }
    else if (strcmp(a0, "mkdir") == 0) {
        if (argc < 2) term_print(t, "usage: mkdir [-p] <path>");
        else if (strcmp(argv[1], "-p") == 0) { if (argc > 2) term_mkdir_p(t, argv[2]); else term_print(t, "usage: mkdir -p <path>"); }
        else if (!g_vfs->mkdir(argv[1])) { String e = "mkdir: failed: "; e += argv[1]; term_print(t, e.c_str()); }
    }
    else if (strcmp(a0, "touch") == 0) {
        if (argc < 2) term_print(t, "usage: touch <file>");
        else if (!g_vfs->create_file(argv[1])) { String e = "touch: failed: "; e += argv[1]; term_print(t, e.c_str()); }
    }
    else if (strcmp(a0, "rm") == 0) {
        if (argc < 2) term_print(t, "usage: rm <path>");
        else {
            FSNode* n = g_vfs->resolve(argv[1]);
            if (!n) { String e = "rm: not found: "; e += argv[1]; term_print(t, e.c_str()); }
            else if (!g_vfs->remove_node(n)) term_print(t, "rm: cannot remove root");
        }
    }
    else if (strcmp(a0, "echo") == 0) {
        // 检查重定向
        int redir = -1;
        for (int i = 1; i < argc; i++) if (strcmp(argv[i], ">") == 0) { redir = i; break; }
        if (redir > 0 && redir + 1 < argc) {
            String text;
            for (int i = 1; i < redir; i++) { if (i > 1) text += ' '; text += argv[i]; }
            FSNode* f = g_vfs->create_file(argv[redir + 1]);
            if (f) {
                g_vfs->write_file(f, (const uint8_t*)text.c_str(), (uint32_t)text.len());
                String ok = "written: "; ok += argv[redir + 1]; term_print(t, ok.c_str());
            } else term_print(t, "echo: cannot write file");
        } else {
            String text;
            for (int i = 1; i < argc; i++) { if (i > 1) text += ' '; text += argv[i]; }
            term_print(t, text.c_str());
        }
    }
    else if (strcmp(a0, "whoami") == 0) term_print(t, "nefu");
    else if (strcmp(a0, "uname") == 0) term_print(t, "nefuOS 0.2.0 x86_64");
    else if (strcmp(a0, "date") == 0) {
        char buf[64];
        uint32_t s = nefuos_uptime_ms() / 1000;
        ksprintf(buf, sizeof(buf), "uptime %u s - epoch 2026-09-06 00:00:00 +%us", s, s);
        term_print(t, buf);
    }
    else if (strcmp(a0, "free") == 0) {
        term_print(t, "              total        used        free");
        term_print(t, "Mem:         65536 KB     ~4096 KB    ~61440 KB");
        char buf[64];
        ksprintf(buf, sizeof(buf), "VFS files:   %d nodes, %u bytes", g_vfs->node_count(), g_vfs->total_bytes());
        term_print(t, buf);
    }
    else if (strcmp(a0, "ps") == 0) {
        term_print(t, "PID  NAME            STATE");
        term_print(t, "  1  desktop         running");
        term_print(t, "  2  window manager  running");
        term_print(t, "  3  vfs daemon      running");
        term_print(t, "  4  network stack   running");
        term_print(t, "  5  shell           running");
    }
    else if (strcmp(a0, "netstat") == 0) {
        char buf[96];
        ksprintf(buf, sizeof(buf), "e1000 %s ip=10.0.2.15 gw=10.0.2.2",
                 g_net.up ? "up" : "down");
        term_print(t, buf);
        ksprintf(buf, sizeof(buf), "rx=%u tx=%u arp=%u icmp=%u tcp=%u",
                 g_net.rx_count, g_net.tx_count, g_net.arp_reqs, g_net.icmp_reqs, g_net.tcp_conns);
        term_print(t, buf);
        if (!g_net.up) term_print(t, "note: no NIC on this backend");
    }
    else if (strcmp(a0, "ping") == 0) {
        uint32_t ip = argc > 1 ? parse_ip(argv[1]) : g_net.gw;
        char buf[64];
        ksprintf(buf, sizeof(buf), "ping %d.%d.%d.%d ...",
                 (int)((ip >> 24) & 0xFF), (int)((ip >> 16) & 0xFF),
                 (int)((ip >> 8) & 0xFF), (int)(ip & 0xFF));
        term_print(t, buf);
        if (!g_net.up) { term_print(t, "network not available"); }
        else {
            bool ok = net_ping(ip, 1500);
            ksprintf(buf, sizeof(buf), "ping %s (%u ms)", ok ? "OK" : "FAIL",
                     (unsigned)(nefuos_uptime_ms() % 100000));
            term_print(t, buf);
        }
    }
    else if (strcmp(a0, "cp") == 0) {
        if (argc < 3) term_print(t, "usage: cp <src> <dst>");
        else {
            FSNode* src = g_vfs->resolve(argv[1]);
            if (!src || src->is_dir) term_print(t, "cp: source not a file");
            else if (fs_copy(src, argv[2])) { String ok = "copied: "; ok += argv[2]; term_print(t, ok.c_str()); }
            else term_print(t, "cp: copy failed");
        }
    }
    else if (strcmp(a0, "mv") == 0) {
        if (argc < 3) term_print(t, "usage: mv <src> <dst>");
        else {
            FSNode* src = g_vfs->resolve(argv[1]);
            if (!src || src->is_dir) term_print(t, "mv: source not a file");
            else if (fs_copy(src, argv[2]) && g_vfs->remove_node(src)) {
                String ok = "moved: "; ok += argv[2]; term_print(t, ok.c_str());
            } else term_print(t, "mv: move failed");
        }
    }
    else if (strcmp(a0, "find") == 0) {
        FSNode* d = argc > 1 ? g_vfs->resolve(argv[1]) : g_vfs->root();
        if (!d) term_print(t, "find: bad path");
        else {
            struct Walk {
                static void go(FSNode* n, int depth, TermState* tt) {
                    String line;
                    for (int i = 0; i < depth; i++) line += "  ";
                    line += n->name;
                    if (n->is_dir) line += "/";
                    term_add(tt, line.c_str());
                    for (int i = 0; i < n->children.size(); i++) go(n->children[i], depth + 1, tt);
                }
            };
            Walk::go(d, 0, t);
        }
    }
    else if (strcmp(a0, "run") == 0) {
        if (argc < 2) term_print(t, "usage: run <file.nefud>");
        else {
            FSNode* f = g_vfs->resolve(argv[1]);
            if (!f || f->is_dir) { String e = "run: not found: "; e += argv[1]; term_print(t, e.c_str()); }
            else term_nefud_run(t, f);
        }
    }
    else if (strcmp(a0, "clear") == 0) { t->lines.clear(); t->view_scroll = 0; }
    else if (strcmp(a0, "tree") == 0) {
        FSNode* d = argc > 1 ? g_vfs->resolve(argv[1]) : g_vfs->root();
        if (!d) term_print(t, "tree: bad path");
        else {
            struct Walk {
                static void go(FSNode* n, int depth, TermState* tt) {
                    if (depth > 4) return;
                    String line;
                    for (int i = 0; i < depth; i++) line += "  ";
                    line += n->name;
                    if (n->is_dir) line += "/";
                    term_add(tt, line.c_str());
                    for (int i = 0; i < n->children.size(); i++) go(n->children[i], depth + 1, tt);
                }
            };
            Walk::go(d, 0, t);
        }
    }
    else if (strcmp(a0, "about") == 0) {
        term_print(t, "nefuOS v0.1.0 - C++/C tiny OS");
        term_print(t, "dual backend: host + bare-metal");
        term_print(t, "VFS: virtual file tree");
    }
    else if (strcmp(a0, "uptime") == 0) {
        char buf[64];
        uint32_t s = nefuos_uptime_ms() / 1000;
        ksprintf(buf, sizeof(buf), "uptime: %u s (%u min)", s, s / 60);
        term_print(t, buf);
    }
    else if (strcmp(a0, "grep") == 0) {
        if (argc < 3) term_print(t, "usage: grep <pattern> <file>");
        else {
            FSNode* f = g_vfs->resolve(argv[2]);
            if (!f || f->is_dir) term_print(t, "grep: no such file");
            else {
                List<String> ls;
                file_to_lines(f, ls, 4096);
                int hits = 0;
                for (int i = 0; i < ls.size(); i++) {
                    if (ls[i].find(argv[1]) >= 0) { term_print(t, ls[i].c_str()); hits++; }
                }
                char buf[64];
                ksprintf(buf, sizeof(buf), "%d match(es)", hits);
                term_print(t, buf);
            }
        }
    }
    else if (strcmp(a0, "wc") == 0) {
        if (argc < 2) term_print(t, "usage: wc <file>");
        else {
            FSNode* f = g_vfs->resolve(argv[1]);
            if (!f || f->is_dir) term_print(t, "wc: no such file");
            else {
                List<String> ls;
                file_to_lines(f, ls, 4096);
                int words = 0;
                for (int i = 0; i < ls.size(); i++) {
                    const char* p = ls[i].c_str();
                    bool inw = false;
                    while (*p) {
                        if (*p == ' ' || *p == '\t' || *p == '\r') { inw = false; }
                        else if (!inw) { words++; inw = true; }
                        p++;
                    }
                }
                char buf[96];
                ksprintf(buf, sizeof(buf), "lines %d  words %d  bytes %u  %s",
                         ls.size(), words, (unsigned)f->size, f->name.c_str());
                term_print(t, buf);
            }
        }
    }
    else if (strcmp(a0, "head") == 0 || strcmp(a0, "tail") == 0) {
        bool is_head = (a0[0] == 'h');
        if (argc < 2) term_print(t, is_head ? "usage: head <file> [n]" : "usage: tail <file> [n]");
        else {
            int n = 10;
            if (argc > 2) n = 0;
            for (const char* p = argv[2]; *p; p++) n = n * 10 + (*p - '0');
            FSNode* f = g_vfs->resolve(argv[1]);
            if (!f || f->is_dir) term_print(t, "no such file");
            else {
                List<String> ls;
                file_to_lines(f, ls, 4096);
                if (is_head) print_lines_from_file(t, f, 0, n, false);
                else print_lines_from_file(t, f, ls.size() - n, n, false);
            }
        }
    }
    else if (strcmp(a0, "sort") == 0) {
        if (argc < 2) term_print(t, "usage: sort <file>");
        else {
            FSNode* f = g_vfs->resolve(argv[1]);
            if (!f || f->is_dir) term_print(t, "no such file");
            else {
                List<String> ls;
                file_to_lines(f, ls, 4096);
                for (int i = 1; i < ls.size(); i++) {
                    String key = ls[i];
                    int j = i - 1;
                    while (j >= 0 && strcmp(ls[j].c_str(), key.c_str()) > 0) {
                        ls[j + 1] = ls[j]; j--;
                    }
                    ls[j + 1] = key;
                }
                for (int i = 0; i < ls.size(); i++) term_print(t, ls[i].c_str());
            }
        }
    }
    else if (strcmp(a0, "uniq") == 0) {
        if (argc < 2) term_print(t, "usage: uniq <file>");
        else {
            FSNode* f = g_vfs->resolve(argv[1]);
            if (!f || f->is_dir) term_print(t, "no such file");
            else {
                List<String> ls;
                file_to_lines(f, ls, 4096);
                String prev;
                for (int i = 0; i < ls.size(); i++) {
                    if (i == 0 || strcmp(ls[i].c_str(), prev.c_str()) != 0) { term_print(t, ls[i].c_str()); prev = ls[i]; }
                }
            }
        }
    }
    else if (strcmp(a0, "which") == 0) {
        if (argc < 2) term_print(t, "usage: which <command>");
        else {
            char buf[96];
            ksprintf(buf, sizeof(buf), "/bin/%s", argv[1]);
            term_print(t, buf);
            ksprintf(buf, sizeof(buf), "type: shell builtin or /usr/bin/%s", argv[1]);
            term_print(t, buf);
        }
    }
    else if (strcmp(a0, "env") == 0) {
        for (int i = 0; i < 8; i++) {
            char buf[128];
            ksprintf(buf, sizeof(buf), "%s=%s", s_env_names[i], s_env_vals[i]);
            term_print(t, buf);
        }
    }
    else if (strcmp(a0, "export") == 0) {
        if (argc < 2) term_print(t, "usage: export NAME=VALUE");
        else {
            char name[48];
            const char* eq = strchr(argv[1], '=');
            if (!eq) term_print(t, "export: need NAME=VALUE");
            else {
                int nl = (int)(eq - argv[1]);
                if (nl > 47) nl = 47;
                memcpy(name, argv[1], nl); name[nl] = 0;
                int idx = env_index(name);
                if (idx < 0) term_print(t, "export: unknown variable (try USER/HOME/PATH/TERM)");
                else {
                    strncpy(s_env_vals[idx], eq + 1, 95);
                    s_env_vals[idx][95] = 0;
                    char buf[128];
                    ksprintf(buf, sizeof(buf), "%s=%s", name, s_env_vals[idx]);
                    term_print(t, buf);
                }
            }
        }
    }
    else if (strcmp(a0, "history") == 0) {
        for (int i = 0; i < s_hist_n; i++) {
            char buf[140];
            ksprintf(buf, sizeof(buf), "%3d  %s", i + 1, s_history[i]);
            term_print(t, buf);
        }
    }
    else if (strcmp(a0, "df") == 0) {
        term_print(t, "Filesystem      Size     Used  Avail  Use%");
        uint32_t used = g_vfs->total_bytes();
        uint32_t size = 65536 * 1024;
        char buf[96];
        ksprintf(buf, sizeof(buf), "/dev/vfs0       64M   %6uK   %6uK   %3u%%",
                 (unsigned)(used / 1024), (unsigned)((size - used) / 1024),
                 (unsigned)(used * 100 / size));
        term_print(t, buf);
        ksprintf(buf, sizeof(buf), "inodes: %d nodes in tree", g_vfs->node_count());
        term_print(t, buf);
    }
    else if (strcmp(a0, "du") == 0) {
        struct DuWalk {
            static uint32_t go(FSNode* n) {
                uint32_t sz = n->is_dir ? 0 : n->size;
                for (int i = 0; i < n->children.size(); i++) sz += go(n->children[i]);
                return sz;
            }
        };
        FSNode* d = argc > 1 ? g_vfs->resolve(argv[1]) : g_vfs->cwd;
        if (!d) term_print(t, "du: no such path");
        else {
            char buf[64];
            ksprintf(buf, sizeof(buf), "%u bytes", (unsigned)DuWalk::go(d));
            term_print(t, buf);
        }
    }
    else if (strcmp(a0, "chmod") == 0) {
        if (argc < 3) term_print(t, "usage: chmod <mode> <file>  (simulated)");
        else {
            FSNode* f = g_vfs->resolve(argv[2]);
            char buf[96];
            if (f) ksprintf(buf, sizeof(buf), "chmod: %s mode %s (simulated)", f->name.c_str(), argv[1]);
            else ksprintf(buf, sizeof(buf), "chmod: no such file: %s", argv[2]);
            term_print(t, buf);
        }
    }
    else if (strcmp(a0, "cat") == 0 && argc > 1 && strcmp(argv[1], "-n") == 0) {
        if (argc < 3) term_print(t, "usage: cat -n <file>");
        else {
            FSNode* f = g_vfs->resolve(argv[2]);
            if (!f || f->is_dir) term_print(t, "cat: no such file");
            else print_lines_from_file(t, f, 0, 4096, true);
        }
    }
    else if (strcmp(a0, "trash") == 0) {
        if (argc < 2) term_print(t, "usage: trash <file>");
        else {
            FSNode* f = g_vfs->resolve(argv[1]);
            if (!f || f->is_dir) term_print(t, "trash: not a file");
            else if (g_vfs->trash_file(f)) { String ok = "moved to Trash: "; ok += f->name; term_print(t, ok.c_str()); }
            else term_print(t, "trash: failed");
        }
    }
    else if (strcmp(a0, "restore") == 0) {
        if (argc < 2) term_print(t, "usage: restore <file-in-trash>");
        else {
            FSNode* td = g_vfs->trash_dir();
            FSNode* f = td ? g_vfs->resolve_from(td, argv[1]) : 0;
            if (!f || f->is_dir) term_print(t, "restore: not in Trash");
            else if (g_vfs->restore_file(f, g_vfs->cwd)) { String ok = "restored to: "; ok += node_path(g_vfs->cwd); term_print(t, ok.c_str()); }
            else term_print(t, "restore: failed");
        }
    }
    else if (strcmp(a0, "empty-trash") == 0) {
        int c = g_vfs->empty_trash();
        char buf[48];
        ksprintf(buf, sizeof(buf), "trash emptied: %d file(s) removed", c);
        term_print(t, buf);
    }
    else if (strcmp(a0, "exit") == 0) { g_wm->close_window(t->win); }
    else if (strcmp(a0, "shutdown") == 0 || strcmp(a0, "reboot") == 0 || strcmp(a0, "poweroff") == 0) {
        term_print(t, "shutting down...");
        nefuos_shutdown();
        platform_poweroff();
    }
    else if (*a0) {
        String e = "unknown command: ";
        e += a0;
        term_print(t, e.c_str());
    }
}

static void term_paint(Window* w) {
    TermState* t = (TermState*)w->userdata;
    Surface& s = w->back;
    s.fill(TERM_BG);
    int rows = term_rows(t);
    int start = t->lines.size() - (rows - 1) - t->view_scroll;
    if (start < 0) start = 0;
    int y = 0;
    for (int i = start; i < t->lines.size() && y < rows - 1; i++, y++) {
        gfx::text(s, 0, y * 16, t->lines[i].c_str(), TERM_FG, TERM_BG);
    }
    String prompt = prompt_str();
    int py = y * 16;
    gfx::text(s, 0, py, prompt.c_str(), TERM_PM, TERM_BG);
    int px = gfx::text_width(prompt.c_str());
    if (t->cursor > 0) {
        String head = t->input.substr(0, t->cursor);
        gfx::text(s, px, py, head.c_str(), TERM_FG, TERM_BG);
        px += t->cursor * 8;
    }
    if (t->cursor < t->input.len()) {
        gfx::text(s, px, py, t->input.c_str() + t->cursor, TERM_FG, TERM_BG);
    }
    if ((platform_tick_ms() / 400) % 2 == 0) {
        gfx::fillrect(s, px, py, 7, 15, TERM_PM);
    }
}

static void term_key(Window* w, const KeyEvent* e) {
    TermState* t = (TermState*)w->userdata;
    if (!e->down) return;
    if (e->ascii >= 32 && e->ascii < 127) {
        String a = t->input.substr(0, t->cursor);
        String b = t->input.substr(t->cursor, t->input.len() - t->cursor);
        a += e->ascii;
        a += b;
        t->input = a;
        t->cursor++;
        t->view_scroll = 0;
    } else if (e->keycode == KEY_BACKSPACE && t->cursor > 0) {
        String a = t->input.substr(0, t->cursor - 1);
        String b = t->input.substr(t->cursor, t->input.len() - t->cursor);
        a += b;
        t->input = a;
        t->cursor--;
    } else if (e->keycode == KEY_LEFT && t->cursor > 0) t->cursor--;
    else if (e->keycode == KEY_RIGHT && t->cursor < t->input.len()) t->cursor++;
    else if (e->keycode == KEY_HOME) t->cursor = 0;
    else if (e->keycode == KEY_END) t->cursor = t->input.len();
    else if (e->keycode == KEY_ENTER) {
        String prompt = prompt_str();
        String full = prompt + t->input;
        term_add(t, full.c_str());
        term_run(t, t->input.c_str());
        t->input.clear();
        t->cursor = 0;
        t->view_scroll = 0;
    } else if (e->keycode == KEY_ESC) {
        t->input.clear();
        t->cursor = 0;
    }
}

static void term_scroll(Window* w, int delta) {
    TermState* t = (TermState*)w->userdata;
    int step = delta > 0 ? 2 : -2;
    int maxv = t->lines.size();
    t->view_scroll += step;
    if (t->view_scroll < 0) t->view_scroll = 0;
    if (t->view_scroll > maxv) t->view_scroll = maxv;
}

static void term_close(Window* w) {
    if (w->userdata) delete (TermState*)w->userdata;
    w->userdata = 0;
}

void term_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Terminal - nefuOS Shell", x, y, 640, 380);
    if (!w) return;
    TermState* t = new TermState();
    t->win = w;
    t->cursor = 0;
    t->view_scroll = 0;
    w->userdata = t;
    w->on_paint = term_paint;
    w->on_key = term_key;
    w->on_scroll = term_scroll;
    w->on_close = term_close;
    term_print(t, "nefuOS shell v0.1 - type 'help' for commands.");
}

} // namespace nefu
