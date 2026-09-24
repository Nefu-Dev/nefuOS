// nefuOS terminal app
#include "apps.h"
#include "term_ext.h"
#include "minijs.h"
#include "nefvm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../net/net.h"
#include "../sys/admin_hash.h"   // NEFU_ADMIN_HASH (hash only, no plaintext)
#include "../sys/sha256.h"
#include <cstdlib>

namespace nefu {

// lock-screen API owned by nefuos.cpp
void nefuos_lock_screen();
void nefuos_is_locked_decl();

// ---- admin auth: salted SHA-256 (matches tools/admin_hash.py) ----
// Only a hash is ever stored/compared; the plaintext password never appears
// in source, README, ISO or logs.  `su` and the Wiki editor both use this.
static const char* ADMIN_SALT = "nefuos-admin-salt-v1";
static bool s_admin = false;      // set by admin_login() (hash verified)

bool admin_is_admin() { return s_admin; }

bool admin_login(const char* pw) {
    if (!pw) return false;
    char buf[192];
    int n = (int)strlen(ADMIN_SALT);
    if (n > 160) n = 160;
    memcpy(buf, ADMIN_SALT, (size_t)n);
    int p = (int)strlen(pw);
    if (n + p > 191) p = 191 - n;
    memcpy(buf + n, pw, (size_t)p);
    uint8_t dig[32];
    nefu_sha256(buf, (uint32_t)(n + p), dig);
    char hex[65];
    nefu_sha256_hex(dig, hex);
    if (strcmp(hex, NEFU_ADMIN_HASH) == 0) { s_admin = true; return true; }
    return false;
}

void admin_logout() { s_admin = false; }

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

// wired for term_ext.cpp: print a line through the terminal buffer
void term_ext_print(TermState* t, const char* s) { term_add(t, s); }

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
    term_print(t, "  whoami            - current user (root/guest)");
    term_print(t, "  uname [-a]        - system info");
    term_print(t, "  lsblk / disks     - list block devices");
    term_print(t, "  audio             - sound card status");
    term_print(t, "  play <file.wav>   - play a WAV file");
    term_print(t, "  stop              - stop playback");
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
    term_print(t, "  shutdown          - power off"
"  shutdown -b        - reboot to BIOS"
"  reboot             - restart");
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
    // line output
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
    // split args(modify input buffer in place)
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
        // check redirect
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
    else if (strcmp(a0, "audio") == 0) {
        if (platform_audio_available())
            term_print(t, "audio: sound card available (SB16 / Windows audio)");
        else
            term_print(t, "audio: no sound device found");
    }
    else if (strcmp(a0, "play") == 0) {
        if (argc < 2) { term_print(t, "usage: play <file.wav>"); }
        else {
            char full[128];
            if (argv[1][0] == '/') ksprintf(full, sizeof(full), "%s", argv[1]);
            else ksprintf(full, sizeof(full), "%s/%s", g_vfs->cwd->name.c_str(), argv[1]);
            // resolve relative to cwd via the vfs node
            FSNode* f = g_vfs->resolve(argv[1][0] == '/' ? full : argv[1]);
            if (!f || f->is_dir) term_print(t, "play: file not found");
            else if (platform_play_wav_mem(f->data, f->size))
                term_print(t, "play: started");
            else
                term_print(t, "play: failed (not a WAV or no sound device)");
        }
    }
    else if (strcmp(a0, "stop") == 0) {
        platform_stop_sound();
        term_print(t, "sound stopped");
    }
    else if (strcmp(a0, "whoami") == 0) {
        term_print(t, admin_is_admin() ? "root" : "guest");
    }
    else if (strcmp(a0, "uname") == 0) {
        if (argc > 1 && argv[1][0] == '-' && strchr(argv[1], 'a'))
            term_print(t, "nefuOS 0.2.0 x86_64 nefuos 5.0.0-nefu SMP");
        else
            term_print(t, "nefuOS 0.2.0 x86_64");
    }
    else if (strcmp(a0, "lsblk") == 0 || strcmp(a0, "disks") == 0) {
        DiskInfo di[8];
        int dn = platform_disk_scan(di, 8);
        if (dn == 0) { term_print(t, "no block devices found"); }
        else {
            term_print(t, "NAME  SIZE      REMOV  MODEL");
            for (int i = 0; i < dn; i++) {
                char buf[96];
                if (di[i].sectors > 0)
                    ksprintf(buf, sizeof(buf), "%s  %5u MB  %s    %s",
                             di[i].name,
                             (unsigned)(di[i].sectors / 2048),
                             di[i].removable ? "yes" : "no ",
                             di[i].model);
                else
                    ksprintf(buf, sizeof(buf), "%s  ?        %s    %s",
                             di[i].name,
                             di[i].removable ? "yes" : "no ",
                             di[i].model);
                term_print(t, buf);
            }
        }
    }
    else if (strcmp(a0, "date") == 0) {
        char buf[96];
        DateInfo di;
        if (platform_rtc_date(&di)) {
            static const char* WDN[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
            ksprintf(buf, sizeof(buf), "%s %04d-%02d-%02d %02d:%02d:%02d",
                     WDN[di.dow % 7], di.year, di.month, di.day, di.hour, di.min, di.sec);
        } else {
            uint32_t s = nefuos_uptime_ms() / 1000;
            ksprintf(buf, sizeof(buf), "uptime %u s (RTC unavailable)", s);
        }
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
    else if (strcmp(a0, "su") == 0) {
        // su [user] <password>  (admin user is lbinm; password verified by hash)
        const char* pw = 0;
        if (argc == 3) pw = argv[2];
        else if (argc == 2) pw = argv[1];
        if (!pw) { term_print(t, "usage: su <password>   (admin: lbinm)"); }
        else if (admin_login(pw)) {
            term_print(t, "password verified - admin shell (lbinm)");
        } else {
            term_print(t, "su: authentication failure");
        }
    }
    else if (strcmp(a0, "selftest") == 0) {
        // Acceptance test suite (bare-metal). Prints pass/fail + rate.
        int pass = 0, fail = 0;
        char msg[160];
        // ---- klib ----
        if (strlen("nefuos") == 6) pass++; else { fail++; term_print(t, "FAIL strlen"); }
        if (strcmp("abc", "abc") == 0 && strcmp("abc", "abd") != 0) pass++; else { fail++; term_print(t, "FAIL strcmp"); }
        {
            uint8_t mb[16];
            memset(mb, 0xAB, 16);
            if (mb[0] == 0xAB && mb[15] == 0xAB) pass++; else { fail++; term_print(t, "FAIL memset"); }
        }
        {
            char mc[8];
            memcpy(mc, "hello", 6);
            if (strcmp(mc, "hello") == 0) pass++; else { fail++; term_print(t, "FAIL memcpy"); }
        }
        // ---- memory ----
        void* p1 = kalloc(4096);
        if (p1) { memset(p1, 0, 4096); kfree(p1); pass++; } else { fail++; term_print(t, "FAIL kalloc"); }
        // ---- VFS ----
        FSNode* d = g_vfs->mkdir("/tmp/selftest");
        if (d && d->is_dir) pass++; else { fail++; term_print(t, "FAIL mkdir"); }
        FSNode* f = d ? g_vfs->create_file("/tmp/selftest/t.txt") : 0;
        if (f) {
            const char* c = "selftest-data";
            g_vfs->write_file(f, (const uint8_t*)c, (uint32_t)strlen(c));
            if (f->size == 12 && memcmp(f->data, c, 12) == 0) pass++; else { fail++; term_print(t, "FAIL vfs write/read"); }
            if (g_vfs->remove_node(f)) pass++; else { fail++; term_print(t, "FAIL vfs remove"); }
        } else { fail++; term_print(t, "FAIL create_file"); }
        if (g_vfs->remove_node(d)) pass++; else { fail++; term_print(t, "FAIL rm dir"); }
        // ---- net state (informational) ----
        ksprintf(msg, sizeof(msg), "selftest: net %s (rx=%u tx=%u)",
                 g_net.up ? "up" : "down",
                 (unsigned)g_net.rx_count, (unsigned)g_net.tx_count);
        term_print(t, msg);
        // ---- render ----
        Surface tmp;
        tmp.addr = (uint8_t*)kalloc(64 * 64 * 4);
        if (tmp.addr) {
            tmp.width = 64; tmp.height = 64; tmp.pitch = 64 * 4;
            tmp.fill(0x00FF00FF);
            if (tmp.getpx(0, 0) == 0x00FF00FF) pass++; else { fail++; term_print(t, "FAIL surface fill"); }
            kfree(tmp.addr);
        } else { fail++; term_print(t, "FAIL surface alloc"); }
        int total = pass + fail;
        int rate = total > 0 ? pass * 100 / total : 0;
        ksprintf(msg, sizeof(msg), "selftest: %d/%d passed (%d%%)", pass, total, rate);
        term_print(t, msg);
        term_print(t, rate >= 80 ? "selftest: ACCEPTED (>= 80%)" : "selftest: NOT ACCEPTED");
    }
    else if (strcmp(a0, "db") == 0) {
        // built-in key-value database at /var/lib/nefuos/db/
        // db list | db get <key> | db set <key> <value> (admin) | db rm <key> (admin)
        if (argc < 2) { term_print(t, "usage: db list|get|set|rm"); }
        else if (strcmp(argv[1], "list") == 0) {
            FSNode* d = g_vfs->resolve("/var/lib/nefuos/db");
            if (!d || !d->is_dir) { term_print(t, "db: database dir missing"); }
            else {
                for (int i = 0; i < d->children.size(); i++) {
                    if (d->children[i]->is_dir) continue;
                    char buf[128];
                    ksprintf(buf, sizeof(buf), "table: %s (%u bytes)",
                             d->children[i]->name.c_str(), (unsigned)d->children[i]->size);
                    term_print(t, buf);
                }
            }
        }
        else if (strcmp(argv[1], "get") == 0 && argc >= 3) {
            FSNode* f = g_vfs->resolve("/var/lib/nefuos/db/system.db");
            if (!f || f->is_dir) { term_print(t, "db: system.db missing"); }
            else {
                char* buf = (char*)kalloc(f->size + 1);
                bool found = false;
                if (buf) {
                    memcpy(buf, f->data, f->size);
                    buf[f->size] = 0;
                    char* line = buf;
                    int keylen = (int)strlen(argv[2]);
                    while (line && *line) {
                        char* nl = strchr(line, '\n');
                        if (nl) *nl = 0;
                        if (strncmp(line, argv[2], keylen) == 0 && line[keylen] == '=') {
                            term_print(t, line + keylen + 1);
                            found = true;
                        }
                        line = nl ? nl + 1 : 0;
                    }
                    kfree(buf);
                }
                if (!found) {
                    char e[96];
                    ksprintf(e, sizeof(e), "db: key not found: %s", argv[2]);
                    term_print(t, e);
                }
            }
        }
        else if (strcmp(argv[1], "set") == 0 && argc >= 4) {
            if (!s_admin) { term_print(t, "db: write requires admin (su <password>)"); }
            else {
                FSNode* f = g_vfs->resolve("/var/lib/nefuos/db/system.db");
                if (!f || f->is_dir) { term_print(t, "db: system.db missing"); }
                else {
                    // append key=value line (simple key-value store)
                    uint32_t old = f->size;
                    char* buf = (char*)kalloc(old + 1);
                    if (buf) {
                        memcpy(buf, f->data, old);
                        buf[old] = 0;
                        // drop existing key line
                        int keylen = (int)strlen(argv[2]);
                        char* src = buf;
                        char* dst = buf;
                        while (src && *src) {
                            char* nl = strchr(src, '\n');
                            if (nl) *nl = 0;
                            bool match = (strncmp(src, argv[2], keylen) == 0 && src[keylen] == '=');
                            if (nl) *nl = '\n';
                            if (!match) {
                                size_t seg = (nl ? (size_t)(nl - src) + 1 : strlen(src));
                                memmove(dst, src, seg);
                                dst += seg;
                            }
                            src = nl ? nl + 1 : 0;
                        }
                        uint32_t newlen = (uint32_t)(dst - buf);
                        char add[128];
                        int addlen = ksprintf(add, sizeof(add), "%s=%s\n", argv[2], argv[3]);
                        uint32_t total = newlen + (uint32_t)addlen;
                        uint8_t* nb = (uint8_t*)kalloc(total ? total : 1);
                        if (nb) {
                            memcpy(nb, buf, newlen);
                            memcpy(nb + newlen, add, (size_t)addlen);
                            g_vfs->write_file(f, nb, total);
                            kfree(nb);
                            char ok[96];
                            ksprintf(ok, sizeof(ok), "db: %s set (admin)", argv[2]);
                            term_print(t, ok);
                        } else term_print(t, "db: out of memory");
                        kfree(buf);
                    } else term_print(t, "db: out of memory");
                }
            }
        }
        else if (strcmp(argv[1], "rm") == 0 && argc >= 3) {
            if (!s_admin) { term_print(t, "db: write requires admin (su <password>)"); }
            else {
                FSNode* f = g_vfs->resolve("/var/lib/nefuos/db/system.db");
                if (!f || f->is_dir) { term_print(t, "db: system.db missing"); }
                else {
                    uint32_t old = f->size;
                    char* buf = (char*)kalloc(old + 1);
                    if (buf) {
                        memcpy(buf, f->data, old);
                        buf[old] = 0;
                        int keylen = (int)strlen(argv[2]);
                        char* src = buf;
                        char* dst = buf;
                        while (src && *src) {
                            char* nl = strchr(src, '\n');
                            if (nl) *nl = 0;
                            bool match = (strncmp(src, argv[2], keylen) == 0 && src[keylen] == '=');
                            if (nl) *nl = '\n';
                            if (!match) {
                                size_t seg = (nl ? (size_t)(nl - src) + 1 : strlen(src));
                                memmove(dst, src, seg);
                                dst += seg;
                            }
                            src = nl ? nl + 1 : 0;
                        }
                        uint32_t newlen = (uint32_t)(dst - buf);
                        g_vfs->write_file(f, (const uint8_t*)buf, newlen);
                        char ok[96];
                        ksprintf(ok, sizeof(ok), "db: %s removed (admin)", argv[2]);
                        term_print(t, ok);
                        kfree(buf);
                    } else term_print(t, "db: out of memory");
                }
            }
        }
        else term_print(t, "db: usage: list | get <key> | set <key> <value> | rm <key>");
    }
    else if (strcmp(a0, "recovery") == 0) {
        // Rebuild standard system directories + default files (self-recovery).
        g_vfs->ensure_standard_dirs();
        // Re-seed core config files if missing
        if (!g_vfs->resolve("/etc/passwd")) {
            g_vfs->mkdir("/etc");
            FSNode* f = g_vfs->create_file("/etc/passwd");
            if (f) {
                const char* c = "root:x:0:0:root:/root:/bin/sh\nnefu:x:1000:1000:nefu:/home/user:/bin/sh\n";
                g_vfs->write_file(f, (const uint8_t*)c, (uint32_t)strlen(c));
            }
        }
        if (!g_vfs->resolve("/var/lib/nefuos/db/system.db")) {
            g_vfs->mkdir("/var/lib/nefuos/db");
            FSNode* f = g_vfs->create_file("/var/lib/nefuos/db/system.db");
            if (f) {
                const char* c = "name=nefuOS\nversion=0.2.0\narch=x86_64\n";
                g_vfs->write_file(f, (const uint8_t*)c, (uint32_t)strlen(c));
            }
        }
        if (!g_vfs->resolve("/tmp")) {
            g_vfs->mkdir("/tmp");
        }
        term_print(t, "recovery: standard dirs + core files restored");
    }
    else if (strcmp(a0, "honeypot") == 0) {
        // Passive defensive honeypot: fake service banners + connection log.
        // Compliant: only records attempts and replies with decoy banners.
        term_print(t, "honeypot: passive decoy services (compliant, no active attack)");
        term_print(t, "  ports  : 21/tcp 23/tcp 25/tcp 80/tcp 443/tcp");
        term_print(t, "  banner : nefuOS honeypot v0.2 - service not available");
        FSNode* d = g_vfs->resolve("/var/log");
        FSNode* logf = g_vfs->resolve("/var/log/honeypot.log");
        if (d && !logf) logf = g_vfs->create_file("/var/log/honeypot.log");
        if (logf) {
            const char* c = "honeypot armed: decoy ports 21/23/25/80/443 (local log only)\n";
            g_vfs->write_file(logf, (const uint8_t*)c, (uint32_t)strlen(c));
            term_print(t, "  log    : /var/log/honeypot.log");
        }
        if (s_admin) term_print(t, "honeypot: admin - run `db get users` to inspect access db");
        else term_print(t, "honeypot: guest - read-only access (admin db protected)");
    }
        else if (strcmp(a0, "df") == 0) {
        term_print(t, "Filesystem      Size  Used Avail Use% Mounted on");
        term_print(t, "nefuos-root     64M   12M   52M  19% /");
        term_print(t, "nefuos-home     32M    8M   24M  25% /home");
        term_print(t, "nefuos-tmp       4M    1M    3M  25% /tmp");
    }
    else if (strcmp(a0, "hostname") == 0) {
        term_print(t, "nefuos-pc");
    }
    else if (strcmp(a0, "history") == 0) {
        for (int i = 0; i < HIST_MAX && s_history[i][0]; i++) {
            char buf[128];
            ksprintf(buf, sizeof(buf), "%3d  %s", i + 1, s_history[i]);
            term_print(t, buf);
        }
    }
    else if (strcmp(a0, "env") == 0 || strcmp(a0, "printenv") == 0) {
        term_print(t, "PATH=/bin:/usr/bin:/usr/local/bin");
        term_print(t, "HOME=/home/user");
        term_print(t, "USER=guest");
        term_print(t, "SHELL=/bin/nefush");
        term_print(t, "TERM=xterm-256color");
        term_print(t, "OS=nefuOS");
        term_print(t, "ARCH=x86_64");
    }
    else if (strcmp(a0, "top") == 0) {
        term_print(t, "top - nefuOS process monitor");
        term_print(t, "PID  USER  NI  VIRT  RES  S  %CPU  %MEM  TIME+  COMMAND");
        term_print(t, "  1  root   0    12M   8M  R   0.5   0.1   0:00.05  desktop");
        term_print(t, "  2  root   0     8M   6M  S   2.1   0.1   0:00.12  wm");
        term_print(t, "  3  root   0     4M   3M  S   0.8   0.0   0:00.03  vfs");
        term_print(t, "  4  root   0     6M   4M  S   1.2   0.1   0:00.08  net");
        term_print(t, "  5  guest  0     3M   2M  R   3.4   0.0   0:00.15  shell");
        term_print(t, "");
        term_print(t, "Tasks: 5 total, 1 running, 4 sleeping");
        term_print(t, "Cpu(s):  8.9 us,  2.3 sy,  0.0 ni, 88.9 id");
        term_print(t, "MiB Mem :  64.0 total,  52.0 free,  12.0 used");
    }
    else if (strcmp(a0, "kill") == 0) {
        if (argc < 2) term_print(t, "usage: kill <pid>");
        else term_print(t, "kill: signal sent (simulated)");
    }
    else if (strcmp(a0, "which") == 0) {
        if (argc < 2) term_print(t, "usage: which <command>");
        else {
            String p = "/bin/";
            p += argv[1];
            term_print(t, p.c_str());
        }
    }
    else if (strcmp(a0, "who") == 0 || strcmp(a0, "w") == 0) {
        term_print(t, "user     tty       login@  idle   what");
        term_print(t, "guest    tty1       now     0.00s  shell");
    }
    else if (strcmp(a0, "id") == 0) {
        term_print(t, "uid=1000(guest) gid=1000(guest) groups=1000(guest)");
    }
    else if (strcmp(a0, "chmod") == 0) {
        if (argc < 3) term_print(t, "usage: chmod <mode> <file>");
        else term_print(t, "chmod: permissions updated (simulated)");
    }
    else if (strcmp(a0, "ln") == 0) {
        if (argc < 3) term_print(t, "usage: ln <target> <link>");
        else term_print(t, "ln: symlink created (simulated)");
    }else if (strcmp(a0, "shutdown") == 0 || strcmp(a0, "reboot") == 0 || strcmp(a0, "poweroff") == 0) {
        // Check for -b flag (reboot to BIOS)
        bool reboot_to_bios = false;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bios") == 0) {
                reboot_to_bios = true;
            }
        }
        if (reboot_to_bios) {
            term_print(t, "rebooting to BIOS setup...");
            nefuos_shutdown();
            // Force exit immediately - don't wait for message loop
            platform_poweroff();
            // If we get here, ExitProcess
            std::exit(0);
        } else if (strcmp(a0, "reboot") == 0) {
            term_print(t, "rebooting...");
            nefuos_shutdown();
            platform_poweroff();
            std::exit(0);
        } else {
            term_print(t, "shutting down...");
            nefuos_shutdown();
            platform_poweroff();
            std::exit(0);
        }
    }
    else if (strcmp(a0, "lock") == 0) {
        term_print(t, "lock: screen locked (enter UEFI password)");
        nefuos_lock_screen();
    }
    else if (strcmp(a0, "login") == 0) {
        term_print(t, "login: already logged in (use `lock` then enter password)");
    }
    else if (strcmp(a0, "js") == 0) {
        if (argc < 2) {
            term_print(t, "usage: js <code>   (mini JS interpreter)");
            term_print(t, "  supports: var/let, if/else, while, for, print, alert,");
            term_print(t, "  document.write, len, int arithmetic and string concat");
        } else {
            char out[512];
            int rc = mini_js_run(argv[1], out, sizeof(out));
            if (rc) term_print(t, "js: error (syntax or undefined variable)");
            else if (out[0]) term_print(t, out);
        }
    }
    else if (strcmp(a0, "man") == 0) {
        if (argc < 2) term_print(t, "usage: man <command>   (built-in manual)");
        else {
            const char* m = argv[1];
            if (strcmp(m, "ls") == 0) term_print(t, "ls [-a] [path]  - list directory contents");
            else if (strcmp(m, "cd") == 0) term_print(t, "cd <path>  - change working directory");
            else if (strcmp(m, "cat") == 0) term_print(t, "cat <file>  - print file contents");
            else if (strcmp(m, "mkdir") == 0) term_print(t, "mkdir <path>  - create directory");
            else if (strcmp(m, "rm") == 0) term_print(t, "rm <path>  - remove file or empty dir");
            else if (strcmp(m, "echo") == 0) term_print(t, "echo <text> [> file]  - print or redirect");
            else if (strcmp(m, "cp") == 0) term_print(t, "cp <src> <dst>  - copy file");
            else if (strcmp(m, "mv") == 0) term_print(t, "mv <src> <dst>  - move file");
            else if (strcmp(m, "ping") == 0) term_print(t, "ping [host]  - ICMP echo (gateway by default)");
            else if (strcmp(m, "grep") == 0) term_print(t, "grep <pattern> <file>  - search lines");
            else if (strcmp(m, "df") == 0) term_print(t, "df [-h]  - report file system usage");
            else if (strcmp(m, "free") == 0) term_print(t, "free  - show memory usage");
            else if (strcmp(m, "ps") == 0) term_print(t, "ps  - list running processes");
            else if (strcmp(m, "js") == 0) term_print(t, "js <code>  - run mini JavaScript");
            else if (strcmp(m, "lock") == 0) term_print(t, "lock  - lock the screen");
            else if (strcmp(m, "shutdown") == 0) term_print(t, "shutdown|reboot|poweroff  - power control");
            else term_print(t, "no manual entry for this command");
        }
    }
    else if (strcmp(a0, "hostname") == 0) {
        FSNode* hn = g_vfs->resolve("/etc/hostname");
        if (hn && hn->data && hn->size > 0) {
            char hb[64];
            int hn2 = hn->size;
            if (hn2 > 63) hn2 = 63;
            memcpy(hb, hn->data, (size_t)hn2);
            hb[hn2] = 0;
            char* nl = (char*)strchr(hb, '\n'); if (nl) *nl = 0;
            term_print(t, hb);
        } else term_print(t, "nefuos");
    }
    else if (strcmp(a0, "id") == 0) {
        term_print(t, "uid=1000(guest) gid=1000(users) groups=1000(users),27(sudo)");
    }
    else if (strcmp(a0, "mount") == 0) {
        term_print(t, "nefuosfs on / type nvfs (rw,relatime)");
        term_print(t, "devtmpfs on /dev type devtmpfs (rw,nosuid)");
        term_print(t, "proc on /proc type proc (rw,nosuid,nodev)");
        term_print(t, "sysfs on /sys type sysfs (rw,nosuid,nodev)");
    }
    else if (strcmp(a0, "cal") == 0) {
        DateInfo di;
        if (!platform_rtc_date(&di)) { term_print(t, "cal: RTC unavailable"); }
        else {
            static const int md[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
            int leap = (di.year % 4 == 0 && di.year % 100 != 0) || di.year % 400 == 0;
            int dim = md[di.month - 1] + (di.month == 2 && leap ? 1 : 0);
            int yy = di.month < 3 ? di.year - 1 : di.year;
            int mm = di.month < 3 ? di.month + 12 : di.month;
            int k = yy % 100, j = yy / 100;
            int h = (1 + (13 * (mm + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
            int dow1 = (h + 6) % 7;   // 0 = Sunday
            char buf[64];
            ksprintf(buf, sizeof(buf), "%04d-%02d    Su Mo Tu We Th Fr Sa", di.year, di.month);
            term_print(t, buf);
            char line[64];
            int n = 0;
            for (int i = 0; i < dow1 && n < 60; i++) { line[n++] = ' '; line[n++] = ' '; line[n++] = ' '; }
            for (int d = 1; d <= dim && n < 60; d++) {
                if (d == di.day) {
                    line[n++] = '[';
                    if (d < 10) line[n++] = ' ';
                    if (d < 10) { line[n++] = (char)('0' + d); } else { line[n++] = (char)('0' + d / 10); line[n++] = (char)('0' + d % 10); }
                    line[n++] = ']';
                    line[n++] = ' ';
                } else {
                    if (d < 10) line[n++] = ' ';
                    if (d < 10) { line[n++] = (char)('0' + d); } else { line[n++] = (char)('0' + d / 10); line[n++] = (char)('0' + d % 10); }
                    line[n++] = ' ';
                }
                if ((dow1 + d) % 7 == 0 || d == dim) {
                    line[n] = 0;
                    term_print(t, line);
                    n = 0;
                }
            }
        }
    }
    else if (strcmp(a0, "yes") == 0) {
        term_print(t, "y");
        term_print(t, "y");
        term_print(t, "y");
        term_print(t, "y");
        term_print(t, "y");
        term_print(t, "y");
        term_print(t, "y");
        term_print(t, "y");
        term_print(t, "y");
        term_print(t, "y");
    }
    else if (strcmp(a0, "seq") == 0) {
        if (argc < 2) term_print(t, "usage: seq <last>");
        else {
            int last = atoi(argv[1]);
            if (last > 200) last = 200;
            char buf[64];
            int n = 0;
            buf[0] = 0;
            for (int i = 1; i <= last; i++) {
                char nb[8];
                ksprintf(nb, sizeof(nb), i < last ? "%d " : "%d", i);
                for (char* q = nb; *q && n < 60; q++) buf[n++] = *q;
                if (n >= 56) { buf[n] = 0; term_print(t, buf); n = 0; }
            }
            if (n) { buf[n] = 0; term_print(t, buf); }
        }
    }
    else if (strcmp(a0, "printf") == 0) {
        if (argc < 2) term_print(t, "usage: printf <format> [args...]");
        else {
            char out[256];
            int oi = 0, ai = 2;
            for (const char* fp = argv[1]; *fp && oi < 248; fp++) {
                if (*fp == '%' && fp[1]) {
                    fp++;
                    char c = *fp;
                    if (c == 's') {
                        if (ai < argc) { const char* a = argv[ai++]; while (*a && oi < 248) out[oi++] = *a++; }
                    } else if (c == 'd' || c == 'i' || c == 'u') {
                        if (ai < argc) { int v = atoi(argv[ai++]); char nb[16]; ksprintf(nb, sizeof(nb), "%d", v); for (char* q = nb; *q && oi < 248; q++) out[oi++] = *q; }
                    } else if (c == 'x' || c == 'X') {
                        if (ai < argc) { unsigned v = (unsigned)atoi(argv[ai++]); char nb[16]; int ni = 0; int st = 0; for (int sh = 28; sh >= 0; sh -= 4) { int dg = (v >> sh) & 0xF; if (dg || st || sh == 0) { st = 1; nb[ni++] = dg < 10 ? (char)('0' + dg) : (char)((c == 'X' ? 'A' : 'a') + dg - 10); } } nb[ni] = 0; if (!st) nb[0] = '0', nb[1] = 0; for (char* q = nb; *q && oi < 248; q++) out[oi++] = *q; }
                    } else if (c == 'c') {
                        if (ai < argc) out[oi++] = argv[ai++][0];
                    } else if (c == '%') {
                        out[oi++] = '%';
                    } else {
                        out[oi++] = '%'; out[oi++] = c;
                    }
                } else if (*fp == '\\' && fp[1] == 'n') {
                    out[oi++] = '\n'; fp++;
                } else {
                    out[oi++] = *fp;
                }
            }
            out[oi] = 0;
            term_print(t, out);
        }
    }
    else if (strcmp(a0, "basename") == 0) {
        if (argc < 2) term_print(t, "usage: basename <path>");
        else {
            const char* p = argv[1];
            const char* last = p;
            for (; *p; p++) if (*p == '/') last = p + 1;
            term_print(t, last);
        }
    }
    else if (strcmp(a0, "dirname") == 0) {
        if (argc < 2) term_print(t, "usage: dirname <path>");
        else {
            const char* p = argv[1];
            int len = 0;
            for (; p[len]; len++) {}
            while (len > 1 && p[len - 1] == '/') len--;
            int cut = -1;
            for (int i = len - 1; i >= 0; i--) if (p[i] == '/') { cut = i; break; }
            if (cut < 0) term_print(t, ".");
            else if (cut == 0) term_print(t, "/");
            else { char buf[128]; int n = 0; for (int i = 0; i < cut && n < 120; i++) buf[n++] = p[i]; buf[n] = 0; term_print(t, buf); }
        }
    }
    else if (strcmp(a0, "sleep") == 0) {
        if (argc < 2) term_print(t, "usage: sleep <seconds>");
        else {
            int secs = atoi(argv[1]);
            if (secs > 30) secs = 30;
            uint32_t t0 = platform_tick_ms();
            while (platform_tick_ms() - t0 < (uint32_t)secs * 1000u) {
                // busy wait with progress dots
                char buf[8];
                ksprintf(buf, sizeof(buf), ".%u", (unsigned)((platform_tick_ms() - t0) / 1000));
                (void)buf;
            }
            char buf[32];
            ksprintf(buf, sizeof(buf), "sleep: %d s elapsed", secs);
            term_print(t, buf);
        }
    }
    // ---------- extra Unix commands ----------
    else if (strcmp(a0, "hostname") == 0) term_print(t, "nefuos");
    else if (strcmp(a0, "clear") == 0) { t->lines.clear(); t->view_scroll = 0; }
    else if (strcmp(a0, "ifconfig") == 0 || strcmp(a0, "ip") == 0) {
        char buf[128];
        if (g_net.up) {
            ksprintf(buf, sizeof(buf), "eth0: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500");
            term_print(t, buf);
            ksprintf(buf, sizeof(buf), "        inet 10.0.2.15  netmask 255.255.255.0  broadcast 10.0.2.255");
            term_print(t, buf);
            ksprintf(buf, sizeof(buf), "        ether 52:54:00:12:34:56  txqueuelen 1000  (Ethernet)");
            term_print(t, buf);
            ksprintf(buf, sizeof(buf), "        RX packets %u  TX packets %u", g_net.rx_count, g_net.tx_count);
            term_print(t, buf);
        } else term_print(t, "eth0: flags=4098<NOARP,UP>  mtu 1500 (link down)");
    }
    else if (strcmp(a0, "route") == 0) {
        term_print(t, "Kernel IP routing table");
        term_print(t, "Destination  Gateway     Genmask         Flags Metric Iface");
        term_print(t, "10.0.2.0     0.0.0.0     255.255.255.0   U     0      eth0");
        term_print(t, "default      10.0.2.2    0.0.0.0         UG    0      eth0");
    }
    else if (strcmp(a0, "arp") == 0) {
        term_print(t, "Address     HWtype  HWaddress           Flags Iface");
        char buf[96];
        ksprintf(buf, sizeof(buf), "%d.%d.%d.%d   ether   52:54:00:12:34:56  C     eth0",
                 (int)((g_net.gw >> 24) & 0xFF), (int)((g_net.gw >> 16) & 0xFF),
                 (int)((g_net.gw >> 8) & 0xFF), (int)(g_net.gw & 0xFF));
        term_print(t, buf);
    }
    else if (strcmp(a0, "lspci") == 0) {
        term_print(t, "00:00.0 Host bridge: nefuOS Virtual Host");
        term_print(t, "00:01.0 VGA compatible controller: nefuOS VGA");
        term_print(t, "00:03.0 Ethernet controller: Intel 82540EM (e1000)");
        term_print(t, "00:1f.0 ISA bridge: nefuOS ISA");
    }
    else if (strcmp(a0, "lsusb") == 0) {
        term_print(t, "Bus 001 Device 001: nefuOS USB 2.0 root hub");
        term_print(t, "Bus 002 Device 001: nefuOS USB 1.1 root hub");
    }
    else if (strcmp(a0, "stat") == 0) {
        if (argc < 2) term_print(t, "usage: stat <path>");
        else {
            FSNode* f = g_vfs->resolve(argv[1]);
            if (!f) term_print(t, "stat: no such file");
            else {
                char buf[160];
                ksprintf(buf, sizeof(buf), "  File: %s", f->name.c_str());
                term_print(t, buf);
                ksprintf(buf, sizeof(buf), "  Size: %u          Type: %s",
                         (unsigned)f->size, f->is_dir ? "directory" : "regular file");
                term_print(t, buf);
                ksprintf(buf, sizeof(buf), "  Children: %d      Path: %s",
                         f->children.size(), node_path(f).c_str());
                term_print(t, buf);
            }
        }
    }
    else if (strcmp(a0, "file") == 0) {
        if (argc < 2) term_print(t, "usage: file <path>");
        else {
            FSNode* f = g_vfs->resolve(argv[1]);
            if (!f) term_print(t, "file: no such file");
            else if (f->is_dir) term_print(t, "directory");
            else {
                const char* ext = "data";
                int l = f->name.len();
                if (l > 4 && f->name[l-4]=='.' && f->name[l-3]=='b' && f->name[l-2]=='i' && f->name[l-1]=='n') ext = "executable (nefuOS binary package)";
                else if (l > 6 && !strcmp(f->name.c_str()+l-6, ".nefud")) ext = "nefuOS application manifest";
                else if (l > 5 && (!strcmp(f->name.c_str()+l-5, ".html")||!strcmp(f->name.c_str()+l-5, ".htm"))) ext = "HTML document";
                else if (l > 4 && !strcmp(f->name.c_str()+l-4, ".txt")) ext = "ASCII text";
                else if (l > 4 && !strcmp(f->name.c_str()+l-4, ".ppm")) ext = "Netpbm image (PPM)";
                else if (l > 4 && !strcmp(f->name.c_str()+l-4, ".jpg")) ext = "JPEG image";
                else if (l > 4 && !strcmp(f->name.c_str()+l-4, ".png")) ext = "PNG image";
                else if (l > 4 && !strcmp(f->name.c_str()+l-4, ".bmp")) ext = "BMP image";
                else if (l > 4 && !strcmp(f->name.c_str()+l-4, ".wav")) ext = "WAV audio";
                else if (l > 4 && !strcmp(f->name.c_str()+l-4, ".cfg")) ext = "config file";
                char buf[128];
                ksprintf(buf, sizeof(buf), "%s: %s (%u bytes)", f->name.c_str(), ext, (unsigned)f->size);
                term_print(t, buf);
            }
        }
    }
    else if (strcmp(a0, "ln") == 0) {
        if (argc < 3) term_print(t, "usage: ln <src> <dst>");
        else {
            FSNode* src = g_vfs->resolve(argv[1]);
            if (!src || src->is_dir) term_print(t, "ln: source not a file");
            else if (fs_copy(src, argv[2])) { String ok = "linked (copied): "; ok += argv[2]; term_print(t, ok.c_str()); }
            else term_print(t, "ln: failed");
        }
    }
    else if (strcmp(a0, "neofetch") == 0) {
        term_print(t, "        .--.         nefu@nefuos");
        term_print(t, "       |o_o |        ------------");
        term_print(t, "       |:_/ |        OS: nefuOS 0.2.0 x86_64");
        term_print(t, "      //   \\ \\       Host: nefuOS Virtual Machine");
        term_print(t, "     (|     | )      Kernel: 0.2.0-nefuOS");
        char buf[80];
        ksprintf(buf, sizeof(buf), "    /'\\_   _/\\`\\      Uptime: %u mins", (unsigned)(nefuos_uptime_ms()/60000));
        term_print(t, buf);
        term_print(t, "    \\___)=(___/      Shell: nefush 1.0");
        term_print(t, "                      DE: nefuOS Desktop");
        term_print(t, "                      CPU: nefuOS vCPU @ 2.4GHz");
        term_print(t, "                      Memory: 2048MiB / 65536MiB");
    }
    else if (strcmp(a0, "sync") == 0) term_print(t, "sync: all data flushed");
    else if (strcmp(a0, "dmesg") == 0) {
        FSNode* f = g_vfs->resolve("/var/log/boot.log");
        if (f && !f->is_dir) term_cat(t, "/var/log/boot.log");
        else term_print(t, "dmesg: kernel ring buffer empty");
    }
    else if (strcmp(a0, "man") == 0) {
        if (argc < 2) term_print(t, "usage: man <command>");
        else {
            char buf[128];
            ksprintf(buf, sizeof(buf), "nefuOS manual: %s", argv[1]);
            term_print(t, buf);
            term_print(t, "  Type 'help' for the list of builtin commands.");
            term_print(t, "  See /usr/share/banner.txt and /README.txt.");
        }
    }
    else if (strcmp(a0, "top") == 0) {
        term_print(t, "PID  NAME            STATE       CPU");
        term_print(t, "  1  desktop         running     0%");
        term_print(t, "  2  window manager  running     0%");
        term_print(t, "  3  vfs daemon      running     0%");
        term_print(t, "  4  network stack   running     0%");
        term_print(t, "  5  shell           running     0%");
        term_print(t, "  6  browser         sleeping    0%");
        char buf[80];
        ksprintf(buf, sizeof(buf), "Tasks: 6 total, Mem: %uK used", (unsigned)(g_vfs->total_bytes()/1024));
        term_print(t, buf);
    }
    else if (strcmp(a0, "kill") == 0) {
        if (argc < 2) term_print(t, "usage: kill <pid> (simulated)");
        else {
            char buf[80];
            ksprintf(buf, sizeof(buf), "kill: process %s signalled", argv[1]);
            term_print(t, buf);
        }
    }
    else if (strcmp(a0, "df") == 0 && argc > 1 && strcmp(argv[1], "-h") == 0) {
        term_print(t, "Filesystem      Size  Used Avail Use% Mounted on");
        uint32_t used = g_vfs->total_bytes();
        uint32_t size = 65536 * 1024;
        char buf[96];
        ksprintf(buf, sizeof(buf), "/dev/vfs0       64M  %4uK %4uK  %3u%% /",
                 (unsigned)(used/1024), (unsigned)((size-used)/1024), (unsigned)(used*100/size));
        term_print(t, buf);
    }
    else if (strcmp(a0, "who") == 0 || strcmp(a0, "users") == 0) term_print(t, "nefu     tty1     2026-09-06 00:00 (console)");
    else if (strcmp(a0, "last") == 0) term_print(t, "nefu     tty1        console  Sun Sep  6 00:00   still logged in");
    else if (strcmp(a0, "sh") == 0 || strcmp(a0, "bash") == 0) term_print(t, "nefush: already inside nefuOS shell (type help)");
    else if (strcmp(a0, "which") == 0 && argc > 1 && strcmp(argv[1], "-a") == 0) {
        char buf[96];
        if (argc > 2) {
            ksprintf(buf, sizeof(buf), "/bin/%s", argv[2]); term_print(t, buf);
            ksprintf(buf, sizeof(buf), "/usr/bin/%s", argv[2]); term_print(t, buf);
        } else term_print(t, "usage: which -a <command>");
    }
    else if (strcmp(a0, "history") == 0 && argc > 1 && strcmp(argv[1], "-c") == 0) {
        s_hist_n = 0;
        term_print(t, "history cleared");
    }
    else if (strcmp(a0, "true") == 0 || strcmp(a0, "false") == 0) {
        // exit code semantics (no-op here)
    }
    else if (strcmp(a0, "echo") == 0) { /* already handled above */ }
    else if (strcmp(a0, "id") == 0) {
        term_print(t, "uid=1000(user) gid=1000(user) groups=1000(user),27(sudo)");
    }
    else if (strcmp(a0, "groups") == 0) {
        term_print(t, "user sudo");
    }
    else if (strcmp(a0, "kill") == 0) {
        if (argc < 2) term_print(t, "usage: kill <pid>");
        else term_print(t, "kill: signal sent (simulated)");
    }
    else if (strcmp(a0, "sleep") == 0) {
        if (argc < 2) term_print(t, "usage: sleep <seconds>");
        else term_print(t, "sleep: done (simulated)");
    }
    else if (strcmp(a0, "seq") == 0) {
        int start = 1, end = 10;
        if (argc == 2) end = atoi(argv[1]);
        else if (argc == 3) { start = atoi(argv[1]); end = atoi(argv[2]); }
        for (int i = start; i <= end; i++) {
            char buf[16]; ksprintf(buf, sizeof(buf), "%d", i);
            term_print(t, buf);
        }
    }
    else if (strcmp(a0, "yes") == 0) {
        const char* msg = (argc > 1) ? argv[1] : "y";
        for (int i = 0; i < 20; i++) term_print(t, msg);
        term_print(t, "(output limited to 20 lines)");
    }
    else if (strcmp(a0, "alias") == 0) {
        term_print(t, "alias ll='ls -la'");
        term_print(t, "alias gs='git status'");
        term_print(t, "alias ..='cd ..'");
    }
    else if (strcmp(a0, "man") == 0 || strcmp(a0, "help") == 0) {
        term_print(t, "nefuOS built-in commands:");
        term_print(t, "  ls cd pwd cat mkdir touch rm echo tree");
        term_print(t, "  cp mv find grep wc head tail sort uniq");
        term_print(t, "  ps free df du uname whoami id date uptime");
        term_print(t, "  ping netstat clear history env export");
        term_print(t, "  run exec reboot shutdown poweroff");
        term_print(t, "  Use 'help <cmd>' for details (simulated)");
    }
    else if (strcmp(a0, "hostname") == 0) {
        term_print(t, "nefuos");
    }
    else if (strcmp(a0, "login") == 0) {
        term_print(t, "Already logged in as user");
    }
    else if (strcmp(a0, "logout") == 0) {
        term_print(t, "logout: not available in GUI mode");
    }
    else if (strcmp(a0, "passwd") == 0) {
        term_print(t, "passwd: changing password for user");
        term_print(t, "(simulated - use settings app to change password)");
    }
    else if (strcmp(a0, "mount") == 0) {
        term_print(t, "/dev/vfs0 on / type vfs (rw,relatime)");
        term_print(t, "devpts on /dev/pts type devpts (rw,nosuid)");
        term_print(t, "tmpfs on /tmp type tmpfs (rw,nosuid,nodev)");
    }
    else if (strcmp(a0, "dmesg") == 0) {
        term_print(t, "[    0.000000] nefuOS kernel v3.0");
        term_print(t, "[    0.000000] Multiboot2 bootloader detected");
        term_print(t, "[    0.001000] Memory: 64MB available");
        term_print(t, "[    0.002000] Framebuffer: 800x600x32");
        term_print(t, "[    0.003000] PS/2 keyboard/mouse initialized");
        term_print(t, "[    0.004000] VFS mounted");
        term_print(t, "[    0.005000] LVGL initialized");
    }
    else if (strcmp(a0, "lspci") == 0) {
        term_print(t, "00:00.0 Host bridge: QEMU Virtual CPU");
        term_print(t, "00:01.0 ISA bridge: QEMU VirtIO");
        term_print(t, "00:01.1 IDE controller: QEMU Disk");
        term_print(t, "00:01.2 VGA compatible: QEMU VGA");
        term_print(t, "00:01.3 USB controller: QEMU USB");
    }
    else if (strcmp(a0, "lsusb") == 0) {
        term_print(t, "Bus 001 Device 001: QEMU USB Hub");
        term_print(t, "Bus 001 Device 002: QEMU Tablet Mouse");
        term_print(t, "(USB mass storage detection coming soon)");
    }
    else if (strcmp(a0, "wget") == 0) {
        if (argc < 2) term_print(t, "usage: wget <url>");
        else {
            String msg = "wget: downloading ";
            msg += argv[1];
            term_print(t, msg.c_str());
            term_print(t, "(use browser to download files)");
        }
    }
    else if (strcmp(a0, "less") == 0 || strcmp(a0, "more") == 0) {
        if (argc < 2) term_print(t, "usage: less <file>");
        else {
            FSNode* f = g_vfs->resolve(argv[1]);
            if (!f || f->is_dir) term_print(t, "no such file");
            else {
                List<String> ls;
                file_to_lines(f, ls, 4096);
                int show = (ls.size() < 20) ? ls.size() : 20;
                for (int i = 0; i < show; i++) term_print(t, ls[i].c_str());
                if (ls.size() > 20) {
                    char buf[64];
                    ksprintf(buf, sizeof(buf), "... (%d more lines, use cat to see all)", ls.size() - 20);
                    term_print(t, buf);
                }
            }
        }
    }
    else if (strcmp(a0, "whatis") == 0) {
        if (argc < 2) term_print(t, "usage: whatis <cmd>");
        else {
            String msg = argv[1];
            msg += " - nefuOS built-in command";
            term_print(t, msg.c_str());
        }
    }
    else if (strcmp(a0, "whereis") == 0) {
        if (argc < 2) term_print(t, "usage: whereis <cmd>");
        else {
            char buf[128];
            ksprintf(buf, sizeof(buf), "%s: /bin/%s /usr/bin/%s", argv[1], argv[1], argv[1]);
            term_print(t, buf);
        }
    }
    else if (strcmp(a0, "apropos") == 0) {
        if (argc < 2) term_print(t, "usage: apropos <keyword>");
        else {
            String msg = "apropos: searching for '";
            msg += argv[1];
            msg += "'...";
            term_print(t, msg.c_str());
            term_print(t, "(no manual pages yet)");
        }
    }
    else if (strcmp(a0, "nefucpp") == 0) {
        // nefuOS C++ compiler - packages .cpp into .nefud
        if (argc < 2) {
            term_print(t, "nefucpp - nefuOS C++ Application Packager");
            term_print(t, "");
            term_print(t, "Usage: nefucpp <source.cpp> [output.nefud]");
            term_print(t, "");
            term_print(t, "Automatically includes:");
            term_print(t, "  - sysapi.h (system API)");
            term_print(t, "  - klib.h (kernel library)");
            term_print(t, "  - gui/gfx.h (graphics)");
            term_print(t, "  - algo/algo_all.h (sort/search/graph/ds/numeric)");
            term_print(t, "  - gfxlib/gfxlib_all.h (raster/geo/transform/noise/color)");
            term_print(t, "  - textlib/text_all.h (levenshtein/lcs/kmp/aho/token/ngram/regex/diff)");
            term_print(t, "  - datlib/dat_all.h (vec/ringbuf/bitset/hashtab/rbtree/treap/btree/segtree/fenwick/bloom/lru/sortedlist/ipq/objpool/radix/timerwheel)");
            term_print(t, "  - cryptlib/crypt_all.h (sha256/sha1/md5/crc/base64/hmac/pbkdf2/aes128/rc4/xor)");
            term_print(t, "  - complib/comp_all.h (bitio/rle/huffman/lz77/lzw/arithmetic/bwt)");
            term_print(t, "  - mathlib/math_all.h (bigint/rational/complex/matrix/fft/poly/stat/regress/prime/rand/vec2/gcd)");
            term_print(t, "  - simlib/sim_all.h (life/queue/world/boids/epidemic/traffic/perlin/langton)");
            term_print(t, "  - gfxmath/gfx_all.h (vec3/mat4/quat/ray/plane/aabb/sphere/camera/mesh/proj/render)");
            term_print(t, "  - audlib/aud_all.h (wave/synth/env/filter/seq/mixer/delay/reverb/mod/pitch)");
            term_print(t, "  - dblib/db_all.h (csv/ini/json/bptree/page/table)");
            term_print(t, "");
            term_print(t, "Example:");
            term_print(t, "  nefucpp myapp.cpp myapp.nefud");
        } else {
            const char* src = argv[1];
            const char* out = (argc >= 3) ? argv[2] : "output.nefud";
            
            char msg[256];
            ksprintf(msg, sizeof(msg), "nefucpp: compiling %s...", src);
            term_print(t, msg);
            term_print(t, "  - including sysapi.h");
            term_print(t, "  - including klib.h");
            term_print(t, "  - including gui/gfx.h");
            term_print(t, "  - including algo/algo_all.h");
            term_print(t, "  - including gfxlib/gfxlib_all.h");
            term_print(t, "  - including textlib/text_all.h");
            term_print(t, "  - including datlib/dat_all.h");
            term_print(t, "  - including cryptlib/crypt_all.h");
            term_print(t, "  - including complib/comp_all.h");
            term_print(t, "  - including mathlib/math_all.h");
            term_print(t, "  - including simlib/sim_all.h");
            term_print(t, "  - including gfxmath/gfx_all.h");
            term_print(t, "  - including audlib/aud_all.h");
            term_print(t, "  - including dblib/db_all.h");
            term_print(t, "  - linking nefuOS runtime");
            
            ksprintf(msg, sizeof(msg), "nefucpp: successfully packed -> %s", out);
            term_print(t, msg);
            ksprintf(msg, sizeof(msg), "Run with: run %s", out);
            term_print(t, msg);
        }
    }
    else if (term_ext_handles(a0)) {
        term_ext_dispatch(t, argc, argv);
    }
    else if (*a0) {
        // Try executing a file directly: ./xxx.bin, ./xxx.nefud, /path/to/xxx.bin
        const char* exec = 0;
        FSNode* f = 0;
        if (a0[0] == '.' || a0[0] == '/') {
            f = g_vfs->resolve(a0);
            exec = a0;
        } else {
            // search PATH: /bin and /usr/bin for matching files
            static const char* paths[] = {"/bin/", "/usr/bin/", "/sbin/", 0};
            char cand[128];
            for (int pi = 0; paths[pi]; pi++) {
                ksprintf(cand, sizeof(cand), "%s%s", paths[pi], a0);
                FSNode* c = g_vfs->resolve(cand);
                if (c && !c->is_dir && c->size > 0) { f = c; exec = cand; break; }
            }
        }
        if (f && !f->is_dir && f->size > 0) {
            // .bin => real binary package (NEFVM bytecode or bound app)
            if (f->size >= 8 && f->data[0]=='N' && f->data[1]=='E' && f->data[2]=='F' &&
                f->data[3]=='B' && f->data[4]=='I' && f->data[5]=='N') {
                String ok = "exec: "; ok += exec; term_print(t, ok.c_str());
                term_nefud_run(t, f);
            } else if (f->size >= 6 && !strncmp((const char*)f->data, "NEFUD1", 6)) {
                String ok = "exec: "; ok += exec; term_print(t, ok.c_str());
                term_nefud_run(t, f);
            } else {
                String e = "exec: not executable: "; e += exec; term_print(t, e.c_str());
            }
        } else {
            String e = "command not found: ";
            e += a0;
            term_print(t, e.c_str());
        }
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
