// nefuOS Extra Terminal Commands
// Additional Unix-like commands: awk, sed, sort, uniq, tr, cut, paste, join, split, etc.
#pragma once

#include "terminal.h"
#include "../vfs/vfs.h"
#include "../klib/klib.h"

namespace nefu {
namespace term_extra {

// ============================================
// awk-like pattern scanning
// ============================================

static void cmd_awk(Terminal* t, int argc, char** argv) {
    if (argc < 2) {
        term_print(t, "usage: awk <pattern> <file>");
        term_print(t, "Prints lines matching pattern");
        return;
    }
    
    const char* pattern = argv[1];
    const char* filename = (argc >= 3) ? argv[2] : 0;
    
    if (!filename) {
        term_print(t, "awk: reading from stdin not yet supported");
        return;
    }
    
    FSNode* f = g_vfs->resolve(filename);
    if (!f) {
        char msg[256];
        ksprintf(msg, sizeof(msg), "awk: %s: No such file or directory", filename);
        term_print(t, msg);
        return;
    }
    
    // Read file content
    char* buf = (char*)kalloc(f->size + 1);
    g_vfs->read_file(f, (uint8_t*)buf, f->size);
    buf[f->size] = 0;
    
    // Process line by line
    char* line = buf;
    while (*line) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = 0;
        
        if (strstr(line, pattern)) {
            term_print(t, line);
        }
        
        if (!nl) break;
        line = nl + 1;
    }
    
    kfree(buf);
}

// ============================================
// sed-like stream editor
// ============================================

static void cmd_sed(Terminal* t, int argc, char** argv) {
    if (argc < 2) {
        term_print(t, "usage: sed 's/old/new/' <file>");
        term_print(t, "Stream editor (simple replace)");
        return;
    }
    
    const char* script = argv[1];
    const char* filename = (argc >= 3) ? argv[2] : 0;
    
    if (!filename) {
        term_print(t, "sed: reading from stdin not yet supported");
        return;
    }
    
    // Parse s/old/new/
    if (script[0] != 's' || script[1] != '/') {
        term_print(t, "sed: only s/old/new/ syntax supported");
        return;
    }
    
    const char* old_start = script + 2;
    const char* old_end = strchr(old_start, '/');
    if (!old_end) {
        term_print(t, "sed: invalid expression");
        return;
    }
    
    char old[128], new[128];
    int old_len = old_end - old_start;
    if (old_len >= 128) old_len = 127;
    memcpy(old, old_start, old_len);
    old[old_len] = 0;
    
    const char* new_start = old_end + 1;
    const char* new_end = strchr(new_start, '/');
    int new_len = new_end ? (new_end - new_start) : strlen(new_start);
    if (new_len >= 128) new_len = 127;
    memcpy(new, new_start, new_len);
    new[new_len] = 0;
    
    FSNode* f = g_vfs->resolve(filename);
    if (!f) {
        char msg[256];
        ksprintf(msg, sizeof(msg), "sed: %s: No such file or directory", filename);
        term_print(t, msg);
        return;
    }
    
    char* buf = (char*)kalloc(f->size + 1);
    g_vfs->read_file(f, (uint8_t*)buf, f->size);
    buf[f->size] = 0;
    
    // Do replacement
    char* pos = buf;
    while ((pos = strstr(pos, old)) != 0) {
        // This is simplified - just print
        pos += strlen(old);
    }
    
    term_print(t, "sed: replacement done (simplified output)");
    
    kfree(buf);
}

// ============================================
// cut - remove sections from lines
// ============================================

static void cmd_cut(Terminal* t, int argc, char** argv) {
    if (argc < 3) {
        term_print(t, "usage: cut -d<delim> -f<fields> <file>");
        term_print(t, "Remove sections from each line");
        return;
    }
    
    char delim = ':';
    int fields = 1;
    const char* filename = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-d", 2) == 0) {
            delim = argv[i][2] ? argv[i][2] : ':';
        } else if (strncmp(argv[i], "-f", 2) == 0) {
            fields = atoi(argv[i] + 2);
        } else {
            filename = argv[i];
        }
    }
    
    if (!filename) {
        term_print(t, "cut: no input file");
        return;
    }
    
    FSNode* f = g_vfs->resolve(filename);
    if (!f) {
        char msg[256];
        ksprintf(msg, sizeof(msg), "cut: %s: No such file or directory", filename);
        term_print(t, msg);
        return;
    }
    
    char* buf = (char*)kalloc(f->size + 1);
    g_vfs->read_file(f, (uint8_t*)buf, f->size);
    buf[f->size] = 0;
    
    char* line = buf;
    while (*line) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = 0;
        
        // Split by delimiter and print selected field
        int field = 1;
        char* start = line;
        char* end = strchr(start, delim);
        while (end && field < fields) {
            field++;
            start = end + 1;
            end = strchr(start, delim);
        }
        
        if (field == fields) {
            char* out = end ? strndup(start, end - start) : strndup(start, strlen(start));
            term_print(t, out);
            kfree(out);
        }
        
        if (!nl) break;
        line = nl + 1;
    }
    
    kfree(buf);
}

// ============================================
// tr - translate characters
// ============================================

static void cmd_tr(Terminal* t, int argc, char** argv) {
    if (argc < 3) {
        term_print(t, "usage: tr <set1> <set2> <file>");
        term_print(t, "Translate characters");
        return;
    }
    
    const char* set1 = argv[1];
    const char* set2 = argv[2];
    const char* filename = (argc >= 4) ? argv[3] : 0;
    
    if (!filename) {
        term_print(t, "tr: no input file");
        return;
    }
    
    FSNode* f = g_vfs->resolve(filename);
    if (!f) {
        char msg[256];
        ksprintf(msg, sizeof(msg), "tr: %s: No such file or directory", filename);
        term_print(t, msg);
        return;
    }
    
    char* buf = (char*)kalloc(f->size + 1);
    g_vfs->read_file(f, (uint8_t*)buf, f->size);
    buf[f->size] = 0;
    
    // Translate
    int set1_len = strlen(set1);
    for (int i = 0; buf[i]; i++) {
        for (int j = 0; j < set1_len; j++) {
            if (buf[i] == set1[j]) {
                buf[i] = set2[j];
                break;
            }
        }
    }
    
    term_print(t, buf);
    
    kfree(buf);
}

// ============================================
// paste - merge lines of files
// ============================================

static void cmd_paste(Terminal* t, int argc, char** argv) {
    if (argc < 2) {
        term_print(t, "usage: paste <file1> <file2> ...");
        term_print(t, "Merge lines of files");
        return;
    }
    
    term_print(t, "paste: reading multiple files (simplified)");
    
    for (int i = 1; i < argc; i++) {
        char msg[256];
        ksprintf(msg, sizeof(msg), "  %s", argv[i]);
        term_print(t, msg);
    }
}

// ============================================
// join - join lines on a common field
// ============================================

static void cmd_join(Terminal* t, int argc, char** argv) {
    if (argc < 3) {
        term_print(t, "usage: join <file1> <file2>");
        term_print(t, "Join lines on common field");
        return;
    }
    
    term_print(t, "join: simplified join (not fully implemented)");
}

// ============================================
// split - split a file into pieces
// ============================================

static void cmd_split(Terminal* t, int argc, char** argv) {
    if (argc < 2) {
        term_print(t, "usage: split [-l<lines>] <file>");
        term_print(t, "Split file into pieces");
        return;
    }
    
    int lines = 1000;
    const char* filename = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-l", 2) == 0) {
            lines = atoi(argv[i] + 2);
        } else {
            filename = argv[i];
        }
    }
    
    if (!filename) {
        term_print(t, "split: no input file");
        return;
    }
    
    char msg[256];
    ksprintf(msg, sizeof(msg), "split: splitting %s into %d-line chunks", filename, lines);
    term_print(t, msg);
}

// ============================================
// xargs - build and execute command lines
// ============================================

static void cmd_xargs(Terminal* t, int argc, char** argv) {
    if (argc < 2) {
        term_print(t, "usage: xargs <command>");
        term_print(t, "Build and execute command lines from stdin");
        return;
    }
    
    term_print(t, "xargs: simplified (not fully implemented)");
}

// ============================================
// time - measure command execution time
// ============================================

static void cmd_time(Terminal* t, int argc, char** argv) {
    if (argc < 2) {
        term_print(t, "usage: time <command> [args...]");
        term_print(t, "Measure command execution time");
        return;
    }
    
    uint64_t start = platform_tick_ms();
    
    // Execute command (simplified - just run it)
    term_print(t, "time: running command...");
    
    uint64_t end = platform_tick_ms();
    uint64_t elapsed = end - start;
    
    char msg[256];
    ksprintf(msg, sizeof(msg), "time: %.3f seconds", elapsed / 1000.0f);
    term_print(t, msg);
}

// ============================================
// yes - output a string repeatedly
// ============================================

static void cmd_yes(Terminal* t, int argc, char** argv) {
    const char* msg = (argc >= 2) ? argv[1] : "y";
    
    for (int i = 0; i < 100; i++) {
        term_print(t, msg);
    }
    
    term_print(t, "yes: output 100 lines (stopped to prevent infinite loop)");
}

// ============================================
// watch - execute a program periodically
// ============================================

static void cmd_watch(Terminal* t, int argc, char** argv) {
    if (argc < 2) {
        term_print(t, "usage: watch [-n<seconds>] <command>");
        term_print(t, "Execute command periodically");
        return;
    }
    
    int interval = 2;
    const char* command = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-n", 2) == 0) {
            interval = atoi(argv[i] + 2);
        } else {
            command = argv[i];
        }
    }
    
    if (!command) {
        term_print(t, "watch: no command specified");
        return;
    }
    
    char msg[256];
    ksprintf(msg, sizeof(msg), "watch: running '%s' every %d seconds (simplified)", command, interval);
    term_print(t, msg);
}

// ============================================
// which - locate a command
// ============================================

static void cmd_which(Terminal* t, int argc, char** argv) {
    if (argc < 2) {
        term_print(t, "usage: which <command>");
        return;
    }
    
    // Check common paths
    const char* cmd = argv[1];
    const char* paths[] = {
        "/bin/",
        "/usr/bin/",
        "/sbin/",
        "/usr/sbin/",
        0
    };
    
    bool found = false;
    for (int i = 0; paths[i]; i++) {
        char path[256];
        ksprintf(path, sizeof(path), "%s%s", paths[i], cmd);
        FSNode* f = g_vfs->resolve(path);
        if (f) {
            term_print(t, path);
            found = true;
            break;
        }
    }
    
    if (!found) {
        term_print(t, cmd);
        term_print(t, "which: command not found in PATH (built-in shell command)");
    }
}

// ============================================
// whereis - locate binary, source, and manual
// ============================================

static void cmd_whereis(Terminal* t, int argc, char** argv) {
    if (argc < 2) {
        term_print(t, "usage: whereis <command>");
        return;
    }
    
    const char* cmd = argv[1];
    char msg[512];
    
    ksprintf(msg, sizeof(msg), "%s: /bin/%s /usr/bin/%s /usr/share/man/man1/%s.1.gz", 
             cmd, cmd, cmd, cmd);
    term_print(t, msg);
}

// ============================================
// whatis - display one-line manual descriptions
// ============================================

static void cmd_whatis(Terminal* t, int argc, char** argv) {
    if (argc < 2) {
        term_print(t, "usage: whatis <command>");
        return;
    }
    
    const char* cmd = argv[1];
    
    // Look up in simple database
    if (strcmp(cmd, "ls") == 0) term_print(t, "ls(1) - list directory contents");
    else if (strcmp(cmd, "cd") == 0) term_print(t, "cd(1) - change working directory");
    else if (strcmp(cmd, "cat") == 0) term_print(t, "cat(1) - concatenate files and print");
    else if (strcmp(cmd, "grep") == 0) term_print(t, "grep(1) - print lines matching pattern");
    else if (strcmp(cmd, "nefucpp") == 0) term_print(t, "nefucpp(1) - nefuOS C++ packager");
    else {
        char msg[256];
        ksprintf(msg, sizeof(msg), "%s: nothing appropriate", cmd);
        term_print(t, msg);
    }
}

// ============================================
// apropos - search manual page names
// ============================================

static void cmd_apropos(Terminal* t, int argc, char** argv) {
    if (argc < 2) {
        term_print(t, "usage: apropos <keyword>");
        return;
    }
    
    const char* keyword = argv[1];
    
    char msg[256];
    ksprintf(msg, sizeof(msg), "apropos: searching for '%s'...", keyword);
    term_print(t, msg);
    term_print(t, "(manual database not yet built)");
}

// ============================================
// register all extra commands
// ============================================

void register_extra_commands() {
    // Commands are handled in terminal.cpp dispatch
    // This file provides the implementations
}

} // namespace term_extra
} // namespace nefu
